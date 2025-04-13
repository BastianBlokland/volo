#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "core_path.h"
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_register.h"
#include "rend_report.h"
#include "trace_tracer.h"

#include "platform_internal.h"
#include "reset_internal.h"
#include "resource_internal.h"
#include "rvk/device_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/mesh_internal.h"
#include "rvk/repository_internal.h"
#include "rvk/shader_internal.h"
#include "rvk/texture_internal.h"

#define rend_res_max_create_time time_millisecond

/**
 * Amount of frames to delay unloading of resources.
 */
#define rend_res_unload_delay 500

typedef struct {
  RvkRepositoryId repoId;
  String          assetId;
  bool            ignoreAssetChanges;
  bool            graphicRequirement; // Delay any graphic creation until this is loaded.
} RendResGlobalDef;

// clang-format off
static const RendResGlobalDef g_rendResGlobal[] = {
  { .repoId = RvkRepositoryId_AmbientDebugGraphic,         .assetId = string_static("graphics/ambient_debug.graphic") },
  { .repoId = RvkRepositoryId_AmbientGraphic,              .assetId = string_static("graphics/ambient.graphic") },
  { .repoId = RvkRepositoryId_AmbientOcclusionGraphic,     .assetId = string_static("graphics/ambient_occlusion.graphic") },
  { .repoId = RvkRepositoryId_BloomDownGraphic,            .assetId = string_static("graphics/bloom_down.graphic") },
  { .repoId = RvkRepositoryId_BloomUpGraphic,              .assetId = string_static("graphics/bloom_up.graphic") },
  { .repoId = RvkRepositoryId_DebugImageViewerCubeGraphic, .assetId = string_static("graphics/dev/image_viewer_cube.graphic") },
  { .repoId = RvkRepositoryId_DebugImageViewerGraphic,     .assetId = string_static("graphics/dev/image_viewer.graphic") },
  { .repoId = RvkRepositoryId_DebugMeshViewerGraphic,      .assetId = string_static("graphics/dev/mesh_viewer.graphic") },
  { .repoId = RvkRepositoryId_FogBlurHorGraphic,           .assetId = string_static("graphics/fog_blur_hor.graphic") },
  { .repoId = RvkRepositoryId_FogBlurVerGraphic,           .assetId = string_static("graphics/fog_blur_ver.graphic") },
  { .repoId = RvkRepositoryId_FogGraphic,                  .assetId = string_static("graphics/fog.graphic") },
  { .repoId = RvkRepositoryId_MissingMesh,                 .assetId = string_static("meshes/missing.procmesh"), .ignoreAssetChanges = true, .graphicRequirement = true },
  { .repoId = RvkRepositoryId_MissingTexture,              .assetId = string_static("textures/missing.proctex"), .ignoreAssetChanges = true, .graphicRequirement = true  },
  { .repoId = RvkRepositoryId_MissingTextureCube,          .assetId = string_static("textures/missing_cube.arraytex"), .ignoreAssetChanges = true, .graphicRequirement = true  },
  { .repoId = RvkRepositoryId_OutlineGraphic,              .assetId = string_static("graphics/outline.graphic") },
  { .repoId = RvkRepositoryId_SkyCubeMapGraphic,           .assetId = string_static("graphics/scene/sky_cubemap.graphic") },
  { .repoId = RvkRepositoryId_SkyGradientGraphic,          .assetId = string_static("graphics/scene/sky_gradient.graphic") },
  { .repoId = RvkRepositoryId_TonemapperGraphic,           .assetId = string_static("graphics/tonemapper.graphic") },
  { .repoId = RvkRepositoryId_WhiteTexture,                .assetId = string_static("textures/white.proctex"), .ignoreAssetChanges = true },
};
// clang-format on

ecs_comp_define_public(RendResGraphicComp);
ecs_comp_define_public(RendResShaderComp);
ecs_comp_define_public(RendResMeshComp);
ecs_comp_define_public(RendResTextureComp);
ecs_comp_define_public(RendResDebugComp);

typedef enum {
  RendResFlags_None               = 0,
  RendResFlags_Used               = 1 << 0,
  RendResFlags_Persistent         = 1 << 1, // Always considered in-use.
  RendResFlags_IgnoreAssetChanges = 1 << 2, // Don't unload when the source asset changes.
} RendResFlags;

typedef enum {
  RendUnloadFlags_UnloadDependents = 1 << 0,
} RendUnloadFlags;

typedef enum {
  RendResLoadState_AssetAcquire,
  RendResLoadState_AssetWait,
  RendResLoadState_DependenciesAcquire,
  RendResLoadState_DependenciesWait,
  RendResLoadState_Create,
  RendResLoadState_UploadWait,
  RendResLoadState_FinishedSuccess,
  RendResLoadState_FinishedFailure,
} RendResLoadState;

typedef enum {
  RendResUnloadState_UnloadDependents,
  RendResUnloadState_UnregisterDependencies,
  RendResUnloadState_Destroy,
  RendResUnloadState_Done,
} RendResUnloadState;

ecs_comp_define(RendResComp) {
  RendResLoadState state : 8;
  RendResFlags     flags : 8;
  u32              unusedTicks;
  DynArray         dependencies; // EcsEntityId[], resources this resource depends on.
  DynArray         dependents;   // EcsEntityId[], resources that depend on this resource.
};
ecs_comp_define(RendResFinishedComp);
ecs_comp_define(RendResUnloadComp) {
  RendResUnloadState state;
  RendUnloadFlags    flags;
};

static void ecs_destruct_graphic_comp(void* data) {
  RendResGraphicComp* comp = data;
  if (comp->report) {
    rend_report_destroy(g_allocHeap, comp->report);
  }
  rvk_graphic_destroy((RvkGraphic*)comp->graphic, comp->device);
}

static void ecs_destruct_shader_comp(void* data) {
  RendResShaderComp* comp = data;
  rvk_shader_destroy((RvkShader*)comp->shader, comp->device);
}

static void ecs_destruct_mesh_comp(void* data) {
  RendResMeshComp* comp = data;
  rvk_mesh_destroy((RvkMesh*)comp->mesh, comp->device);
}

static void ecs_destruct_texture_comp(void* data) {
  RendResTextureComp* comp = data;
  rvk_texture_destroy((RvkTexture*)comp->texture, comp->device);
}

static void ecs_destruct_res_comp(void* data) {
  RendResComp* comp = data;
  dynarray_destroy(&comp->dependencies);
  dynarray_destroy(&comp->dependents);
}

static void rend_res_add_dependency(RendResComp* res, const EcsEntityId dependency) {
  if (!dynarray_search_linear(&res->dependencies, ecs_compare_entity, &dependency)) {
    *dynarray_push_t(&res->dependencies, EcsEntityId) = dependency;
  }
}

static void rend_res_add_dependent(RendResComp* res, const EcsEntityId dependent) {
  if (!dynarray_search_linear(&res->dependents, ecs_compare_entity, &dependent)) {
    *dynarray_push_t(&res->dependents, EcsEntityId) = dependent;
  }
}

static void rend_res_remove_dependent(RendResComp* res, const EcsEntityId dependent) {
  for (usize i = 0; i != res->dependents.size; ++i) {
    if (*dynarray_at_t(&res->dependents, i, EcsEntityId) == dependent) {
      dynarray_remove_unordered(&res->dependents, i, 1);
      return;
    }
  }
}

static void ecs_combine_resource(void* dataA, void* dataB) {
  RendResComp* compA = dataA;
  RendResComp* compB = dataB;
  compA->flags |= compB->flags;
  compA->state = math_max(compA->state, compB->state);

  // Combine dependencies.
  dynarray_for_t(&compB->dependencies, EcsEntityId, entity) {
    rend_res_add_dependency(compA, *entity);
  }
  dynarray_destroy(&compB->dependencies);

  // Combine dependents.
  dynarray_for_t(&compB->dependents, EcsEntityId, entity) {
    rend_res_add_dependent(compA, *entity);
  }
  dynarray_destroy(&compB->dependents);
}

static void ecs_combine_resource_unload(void* dataA, void* dataB) {
  RendResUnloadComp* compA = dataA;
  RendResUnloadComp* compB = dataB;
  compA->flags |= compB->flags;
  compA->state = math_max(compA->state, compB->state);
}

ecs_view_define(PlatReadView) {
  ecs_access_read(RendPlatformComp);
  ecs_access_without(RendResetComp);
}

ecs_view_define(ResWriteView) { ecs_access_write(RendResComp); }

ecs_view_define(ShaderReadView) {
  ecs_access_with(RendResComp);
  ecs_access_read(RendResShaderComp);
}

ecs_view_define(MeshReadView) {
  ecs_access_with(RendResComp);
  ecs_access_read(RendResMeshComp);
}

ecs_view_define(TextureReadView) {
  ecs_access_with(RendResComp);
  ecs_access_read(RendResTextureComp);
}

static const RendResGlobalDef* rend_res_global_lookup(const String assetId) {
  array_for_t(g_rendResGlobal, RendResGlobalDef, res) {
    if (string_eq(assetId, res->assetId)) {
      return res;
    }
  }
  return null;
}

static bool rend_res_request_internal(
    EcsWorld* world, const EcsEntityId assetEntity, const RendResFlags flags) {
  if (ecs_world_has_t(world, assetEntity, RendResUnloadComp)) {
    return false; // Asset is currently in the process of being unloaded.
  }
  ecs_world_add_t(
      world,
      assetEntity,
      RendResComp,
      .flags        = flags | RendResFlags_Used,
      .dependencies = dynarray_create_t(g_allocHeap, EcsEntityId, 0),
      .dependents   = dynarray_create_t(g_allocHeap, EcsEntityId, 0));
  return true;
}

ecs_comp_define(RendResGlobalInitializedComp);
ecs_comp_define(RendResGlobalComp);

ecs_view_define(GlobalResourceInitView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_without(RendResGlobalInitializedComp);
}

ecs_view_define(GlobalResourceRequestView) {
  ecs_access_with(RendResGlobalComp);
  ecs_access_without(RendResComp);
  ecs_access_read(AssetComp);
}

ecs_system_define(RendGlobalResourceUpdateSys) {
  if (ecs_world_has_t(world, ecs_world_global(world), RendResetComp)) {
    return; // Renderer is in the process of being reset.
  }

  EcsIterator* initItr = ecs_view_first(ecs_world_view_t(world, GlobalResourceInitView));
  if (initItr) {
    // Add a 'RendResGlobalComp' component to all global resource assets.
    AssetManagerComp* assetManager = ecs_view_write_t(initItr, AssetManagerComp);
    array_for_t(g_rendResGlobal, RendResGlobalDef, def) {
      const EcsEntityId assetEntity = asset_lookup(world, assetManager, def->assetId);
      ecs_world_add_empty_t(world, assetEntity, RendResGlobalComp);
    }
    ecs_world_add_empty_t(world, ecs_view_entity(initItr), RendResGlobalInitializedComp);
  }

  // Request all global resources to be loaded if they are currently not.
  EcsView* requestView = ecs_world_view_t(world, GlobalResourceRequestView);
  for (EcsIterator* itr = ecs_view_itr(requestView); ecs_view_walk(itr);) {
    const AssetComp*        assetComp = ecs_view_read_t(itr, AssetComp);
    const RendResGlobalDef* def       = rend_res_global_lookup(asset_id(assetComp));
    RendResFlags            flags     = RendResFlags_Persistent;
    if (def->ignoreAssetChanges) {
      flags |= RendResFlags_IgnoreAssetChanges;
    }
    rend_res_request_internal(world, ecs_view_entity(itr), flags);
  }
}

ecs_view_define(ResLoadView) {
  ecs_access_without(RendResFinishedComp);
  ecs_access_read(AssetComp);
  ecs_access_write(RendResComp);

  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_maybe_read(AssetShaderComp);
  ecs_access_maybe_read(AssetMeshComp);
  ecs_access_maybe_read(AssetTextureComp);

  ecs_access_maybe_read(RendResGraphicComp);
  ecs_access_maybe_read(RendResShaderComp);
  ecs_access_maybe_read(RendResMeshComp);
  ecs_access_maybe_read(RendResTextureComp);
}

ecs_view_define(ResLoadDependencyView) {
  ecs_access_write(RendResComp);
  ecs_access_without(RendResUnloadComp);
}

static bool rend_res_asset_acquire(EcsWorld* world, EcsIterator* resourceItr) {
  const EcsEntityId entity = ecs_view_entity(resourceItr);
  asset_acquire(world, entity);
  return true;
}

static bool rend_res_asset_wait(EcsWorld* world, EcsIterator* resourceItr) {
  RendResComp*      resComp = ecs_view_write_t(resourceItr, RendResComp);
  const EcsEntityId entity  = ecs_view_entity(resourceItr);

  if (ecs_world_has_t(world, entity, AssetFailedComp)) {
    resComp->state = RendResLoadState_FinishedFailure;
    return false;
  }
  if (!ecs_world_has_t(world, entity, AssetLoadedComp)) {
    return false;
  }
  if (ecs_world_has_t(world, entity, AssetChangedComp)) {
    log_w(
        "Loaded an out-of-date asset",
        log_param("info", fmt_text_lit("Usually indicates that a changed asset was not released")));
  }
  return true;
}

static bool rend_res_dependencies_acquire(EcsWorld* world, EcsIterator* resourceItr) {
  RendResComp*            resComp           = ecs_view_write_t(resourceItr, RendResComp);
  const AssetGraphicComp* maybeAssetGraphic = ecs_view_read_t(resourceItr, AssetGraphicComp);
  if (maybeAssetGraphic) {
    const RendResFlags depFlags = resComp->flags; // Transfer the flags down to the dependencies.

    heap_array_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
      rend_res_request_internal(world, ptr->program.entity, depFlags);
      rend_res_add_dependency(resComp, ptr->program.entity);
    }

    if (maybeAssetGraphic->mesh.entity) {
      rend_res_request_internal(world, maybeAssetGraphic->mesh.entity, depFlags);
      rend_res_add_dependency(resComp, maybeAssetGraphic->mesh.entity);
    }

    heap_array_for_t(maybeAssetGraphic->samplers, AssetGraphicSampler, ptr) {
      rend_res_request_internal(world, ptr->texture.entity, depFlags);
      rend_res_add_dependency(resComp, ptr->texture.entity);
    }
  }
  return true;
}

static bool rend_res_dependencies_wait(
    const RendPlatformComp* plat, EcsWorld* world, EcsIterator* resourceItr) {
  const EcsEntityId entity         = ecs_view_entity(resourceItr);
  RendResComp*      resComp        = ecs_view_write_t(resourceItr, RendResComp);
  EcsView*          dependencyView = ecs_world_view_t(world, ResLoadDependencyView);
  EcsIterator*      dependencyItr  = ecs_view_itr(dependencyView);

  bool ready = true;
  dynarray_for_t(&resComp->dependencies, EcsEntityId, dep) {
    if (!ecs_view_contains(dependencyView, *dep)) {
      // Re-request the resource as it could have been in the process of being unloaded when we
      // requested it the first time.
      const RendResFlags depFlags = resComp->flags; // Transfer the flags down to the dependencies.
      rend_res_request_internal(world, *dep, depFlags);
      ready = false;
      continue;
    }
    ecs_view_jump(dependencyItr, *dep);
    RendResComp* dependencyRes = ecs_view_write_t(dependencyItr, RendResComp);
    dependencyRes->flags |= RendResFlags_Used; // Mark the dependencies as still in use.
    rend_res_add_dependent(dependencyRes, entity);

    if (ecs_world_has_t(world, *dep, RendResFinishedComp)) {
      if (dependencyRes->state == RendResLoadState_FinishedFailure) {
        // Dependency failed to load, also fail this resource.
        resComp->state = RendResLoadState_FinishedFailure;
        return false;
      }
      diag_assert(dependencyRes->state == RendResLoadState_FinishedSuccess);
    } else {
      ready = false;
    }
  }

  if (ecs_world_has_t(world, entity, AssetGraphicComp)) {
    // Wait for global dependencies to be loaded (for example the 'Missing Texture' asset).
    const RvkRepository* repo = plat->device->repository;
    array_for_t(g_rendResGlobal, RendResGlobalDef, res) {
      if (res->graphicRequirement && !rvk_repository_is_set(repo, res->repoId)) {
        ready = false;
        break;
      }
    }
  }

  return ready;
}

static bool rend_res_create(const RendPlatformComp* plat, EcsWorld* world, EcsIterator* resItr) {
  /**
   * NOTE: We're getting a mutable RvkDevice pointer from a read-access on RendPlatformComp. This
   * means we have to make sure that all api's we use from RvkDevice are actually thread-safe.
   */
  RvkDevice* dev = plat->device;

  const EcsEntityId       entity            = ecs_view_entity(resItr);
  const String            id                = asset_id(ecs_view_read_t(resItr, AssetComp));
  RendResComp*            resComp           = ecs_view_write_t(resItr, RendResComp);
  const bool              resDebug          = ecs_world_has_t(world, entity, RendResDebugComp);
  const AssetGraphicComp* maybeAssetGraphic = ecs_view_read_t(resItr, AssetGraphicComp);
  const AssetShaderComp*  maybeAssetShader  = ecs_view_read_t(resItr, AssetShaderComp);
  const AssetMeshComp*    maybeAssetMesh    = ecs_view_read_t(resItr, AssetMeshComp);
  const AssetTextureComp* maybeAssetTexture = ecs_view_read_t(resItr, AssetTextureComp);

  if (maybeAssetGraphic) {
    RendReport* graphicReport = null;
    if (resDebug) {
      graphicReport = rend_report_create(g_allocHeap, 128 * usize_kibibyte);
    }
    RvkGraphic* graphic = rvk_graphic_create(dev, maybeAssetGraphic, id);

    ecs_world_add_t(
        world,
        entity,
        RendResGraphicComp,
        .device  = dev,
        .graphic = graphic,
        .report  = graphicReport);

    // Add shaders.
    EcsView* shaderView = ecs_world_view_t(world, ShaderReadView);
    heap_array_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
      if (!ecs_view_contains(shaderView, ptr->program.entity)) {
        log_e("Invalid shader reference", log_param("graphic", fmt_text(id)));
        resComp->state = RendResLoadState_FinishedFailure;
        return false;
      }
      EcsIterator* shaderItr = ecs_view_at(shaderView, ptr->program.entity);
      rvk_graphic_add_shader(graphic, ecs_view_read_t(shaderItr, RendResShaderComp)->shader);
    }

    // Add mesh.
    if (maybeAssetGraphic->mesh.entity) {
      EcsView* meshView = ecs_world_view_t(world, MeshReadView);
      if (!ecs_view_contains(meshView, maybeAssetGraphic->mesh.entity)) {
        log_e("Invalid mesh reference", log_param("graphic", fmt_text(id)));
        resComp->state = RendResLoadState_FinishedFailure;
        return false;
      }
      EcsIterator* meshItr = ecs_view_at(meshView, maybeAssetGraphic->mesh.entity);
      rvk_graphic_add_mesh(graphic, ecs_view_read_t(meshItr, RendResMeshComp)->mesh);
    }

    // Add samplers.
    EcsView* textureView = ecs_world_view_t(world, TextureReadView);
    heap_array_for_t(maybeAssetGraphic->samplers, AssetGraphicSampler, ptr) {
      if (!ecs_view_contains(textureView, ptr->texture.entity)) {
        log_e("Invalid texture reference", log_param("graphic", fmt_text(id)));
        resComp->state = RendResLoadState_FinishedFailure;
        return false;
      }
      EcsIterator*              textureItr  = ecs_view_at(textureView, ptr->texture.entity);
      const RendResTextureComp* textureComp = ecs_view_read_t(textureItr, RendResTextureComp);
      rvk_graphic_add_sampler(graphic, maybeAssetGraphic, textureComp->texture, ptr);
    }

    RvkPass* pass = plat->passes[maybeAssetGraphic->pass];
    if (UNLIKELY(!rvk_graphic_finalize(graphic, maybeAssetGraphic, dev, pass, graphicReport))) {
      log_e("Invalid graphic", log_param("graphic", fmt_text(id)));
      resComp->state = RendResLoadState_FinishedFailure;
      return false;
    }
    return true;
  }

  if (maybeAssetShader) {
    RvkShader* shader = rvk_shader_create(dev, maybeAssetShader, id);
    ecs_world_add_t(world, entity, RendResShaderComp, .device = dev, .shader = shader);
    return true;
  }

  if (maybeAssetMesh) {
    RvkMesh* mesh = rvk_mesh_create(dev, maybeAssetMesh, id);
    ecs_world_add_t(world, entity, RendResMeshComp, .device = dev, .mesh = mesh);
    return true;
  }

  if (maybeAssetTexture) {
    RvkTexture* tex = rvk_texture_create(dev, maybeAssetTexture, id);
    ecs_world_add_t(world, entity, RendResTextureComp, .device = dev, .texture = tex);
    return true;
  }

  log_e("Unsupported render resource asset type", log_param("id", fmt_text(id)));
  resComp->state = RendResLoadState_FinishedFailure;
  return false;
}

static bool rend_res_upload_wait(const RendPlatformComp* plat, EcsIterator* resItr) {
  const RvkDevice*        dev       = plat->device;
  const String            id        = asset_id(ecs_view_read_t(resItr, AssetComp));
  const RendResGlobalDef* globalDef = rend_res_global_lookup(id);

  const RendResGraphicComp* maybeGraphic = ecs_view_read_t(resItr, RendResGraphicComp);
  if (maybeGraphic) {
    const bool isReady = rvk_graphic_is_ready(maybeGraphic->graphic, dev);
    if (globalDef && isReady) {
      rvk_repository_graphic_set(dev->repository, globalDef->repoId, maybeGraphic->graphic);
    }
    return isReady;
  }
  const RendResShaderComp* maybeShader = ecs_view_read_t(resItr, RendResShaderComp);
  if (maybeShader) {
    return true; // Shaders do not require uploading.
  }
  const RendResMeshComp* maybeMesh = ecs_view_read_t(resItr, RendResMeshComp);
  if (maybeMesh) {
    const bool isReady = rvk_mesh_is_ready(maybeMesh->mesh, dev);
    if (globalDef && isReady) {
      rvk_repository_mesh_set(dev->repository, globalDef->repoId, maybeMesh->mesh);
    }
    return isReady;
  }
  const RendResTextureComp* maybeTexture = ecs_view_read_t(resItr, RendResTextureComp);
  if (maybeTexture) {
    const bool isReady = rvk_texture_is_ready(maybeTexture->texture, dev);
    if (globalDef && isReady) {
      rvk_repository_texture_set(dev->repository, globalDef->repoId, maybeTexture->texture);
    }
    return isReady;
  }

  diag_crash_msg("Unsupported resource type");
}

static void rend_res_finished_success(EcsWorld* world, EcsIterator* resourceItr) {
  const EcsEntityId entity = ecs_view_entity(resourceItr);

  asset_release(world, entity);
  ecs_world_add_empty_t(world, entity, RendResFinishedComp);
}

static void rend_res_finished_failure(EcsWorld* world, EcsIterator* resourceItr) {
  const EcsEntityId entity = ecs_view_entity(resourceItr);
  const String      id     = asset_id(ecs_view_read_t(resourceItr, AssetComp));

  log_e("Failed to load render resource", log_param("id", fmt_text(id)));

  ecs_utils_maybe_remove_t(world, entity, RendResGraphicComp);
  ecs_utils_maybe_remove_t(world, entity, RendResShaderComp);
  ecs_utils_maybe_remove_t(world, entity, RendResMeshComp);
  ecs_utils_maybe_remove_t(world, entity, RendResTextureComp);

  asset_release(world, entity);
  ecs_world_add_empty_t(world, entity, RendResFinishedComp);
}

static const RendPlatformComp* rend_res_platform(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, PlatReadView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  return itr ? ecs_view_read_t(itr, RendPlatformComp) : null;
}

/**
 * Update all active resource loads.
 */
ecs_system_define(RendResLoadSys) {
  const RendPlatformComp* platform = rend_res_platform(world);
  if (!platform) {
    return;
  }

  TimeDuration loadTime = 0;

  EcsView* resourceView = ecs_world_view_t(world, ResLoadView);
  for (EcsIterator* itr = ecs_view_itr(resourceView); ecs_view_walk(itr);) {
    RendResComp* resComp = ecs_view_write_t(itr, RendResComp);
    switch (resComp->state) {
    case RendResLoadState_AssetAcquire:
      if (!rend_res_asset_acquire(world, itr)) {
        break;
      }
      ++resComp->state;
      break; // NOTE: Cannot fallthrough as asset require takes a frame to take effect.
    case RendResLoadState_AssetWait:
      if (!rend_res_asset_wait(world, itr)) {
        break;
      }
      ++resComp->state;
      // Fallthrough.
    case RendResLoadState_DependenciesAcquire:
      if (!rend_res_dependencies_acquire(world, itr)) {
        break;
      }
      ++resComp->state;
      if (resComp->dependencies.size) {
        break; // NOTE: Cannot fallthrough as dependency acquire takes a frame to take effect.
      }
      // Fallthrough.
    case RendResLoadState_DependenciesWait:
      if (!rend_res_dependencies_wait(platform, world, itr)) {
        break;
      }
      ++resComp->state;
      // Fallthrough.
    case RendResLoadState_Create: {
      if (loadTime >= rend_res_max_create_time) {
        // Already spend our load budget for this frame; retry next frame.
        resComp->state = RendResLoadState_DependenciesWait;
        break;
      }
#ifdef VOLO_TRACE
      const String traceMsg = path_filename(asset_id(ecs_view_read_t(itr, AssetComp)));
#endif
      trace_begin_msg("rend_res_create", TraceColor_Blue, "{}", fmt_text(traceMsg));

      const TimeSteady loadStart = time_steady_clock();
      if (rend_res_create(platform, world, itr)) {
        ++resComp->state;
      } else {
        diag_assert(resComp->state == RendResLoadState_FinishedFailure);
      }
      loadTime += time_steady_duration(loadStart, time_steady_clock());

      trace_end();
    } break;
    case RendResLoadState_UploadWait:
      if (rend_res_upload_wait(platform, itr)) {
        ++resComp->state;
      }
      break;
    case RendResLoadState_FinishedSuccess:
    case RendResLoadState_FinishedFailure:
      UNREACHABLE
    }

    if (resComp->state == RendResLoadState_FinishedSuccess) {
      rend_res_finished_success(world, itr);
    } else if (resComp->state == RendResLoadState_FinishedFailure) {
      rend_res_finished_failure(world, itr);
    }
  }
}

ecs_view_define(ResUnloadUnusedView) {
  ecs_access_write(RendResComp);
  ecs_access_with(RendResFinishedComp);
}

static void rend_res_mark_dependencies_used(const RendResComp* resComp, EcsView* depView) {
  EcsIterator* depItr = ecs_view_itr(depView);
  dynarray_for_t(&resComp->dependencies, EcsEntityId, dep) {
    if (LIKELY(ecs_view_contains(depView, *dep))) {
      ecs_view_jump(depItr, *dep);
      RendResComp* depResComp = ecs_view_write_t(depItr, RendResComp);
      depResComp->flags |= RendResFlags_Used;
    }
  }
}

/**
 * Start unloading resources that have not been used in a while.
 */
ecs_system_define(RendResUnloadUnusedSys) {
  EcsView* resourceUnloadView = ecs_world_view_t(world, ResUnloadUnusedView);

  for (EcsIterator* itr = ecs_view_itr(resourceUnloadView); ecs_view_walk(itr);) {
    RendResComp* resComp = ecs_view_write_t(itr, RendResComp);
    if (LIKELY(resComp->flags & (RendResFlags_Used | RendResFlags_Persistent))) {
      resComp->unusedTicks = 0;
      rend_res_mark_dependencies_used(resComp, resourceUnloadView);
      resComp->flags &= ~RendResFlags_Used;
      continue;
    }
    const EcsEntityId entity      = ecs_view_entity(itr);
    const bool        isUnloading = ecs_world_has_t(world, entity, RendResUnloadComp);
    const bool        failed      = resComp->state == RendResLoadState_FinishedFailure;
    if (UNLIKELY(isUnloading || failed)) {
      continue;
    }
    if (resComp->unusedTicks++ > rend_res_unload_delay) {
      ecs_world_add_t(world, ecs_view_entity(itr), RendResUnloadComp);
    }
  }
}

ecs_view_define(UnloadChangedView) {
  ecs_access_read(AssetComp);
  ecs_access_with(AssetChangedComp);
  ecs_access_read(RendResComp);
  ecs_access_with(RendResFinishedComp);
  ecs_access_without(RendResUnloadComp);
}

/**
 * Start unloading resources when the source asset has changed.
 */
ecs_system_define(RendResUnloadChangedSys) {
  EcsView* changedAssetsView = ecs_world_view_t(world, UnloadChangedView);
  for (EcsIterator* itr = ecs_view_itr(changedAssetsView); ecs_view_walk(itr);) {
    const String id = asset_id(ecs_view_read_t(itr, AssetComp));
    if (ecs_view_read_t(itr, RendResComp)->flags & RendResFlags_IgnoreAssetChanges) {
      continue;
    }
    log_i("Unloading resource due to changed asset", log_param("id", fmt_text(id)));
    ecs_world_add_t(
        world, ecs_view_entity(itr), RendResUnloadComp, .flags = RendUnloadFlags_UnloadDependents);
  }
}

ecs_view_define(UnloadUpdateView) {
  ecs_access_read(RendResComp);
  ecs_access_read(AssetComp);
  ecs_access_write(RendResUnloadComp);
}

/**
 * Update all active resource unloads.
 */
ecs_system_define(RendResUnloadUpdateSys) {
  const RendPlatformComp* platform = rend_res_platform(world);
  if (!platform) {
    return;
  }
  /**
   * NOTE: We're getting a mutable RvkDevice pointer from a read-access on RendPlatformComp. This
   * means we have to make sure that all api's we use from RvkDevice are actually thread-safe.
   */
  RvkDevice* device = platform->device;

  EcsView* unloadView = ecs_world_view_t(world, UnloadUpdateView);

  EcsView*     otherResView = ecs_world_view_t(world, ResWriteView);
  EcsIterator* otherResItr  = ecs_view_itr(otherResView);

  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity     = ecs_view_entity(itr);
    const RendResComp* resComp    = ecs_view_read_t(itr, RendResComp);
    const AssetComp*   assetComp  = ecs_view_read_t(itr, AssetComp);
    RendResUnloadComp* unloadComp = ecs_view_write_t(itr, RendResUnloadComp);
    switch (unloadComp->state) {
    case RendResUnloadState_UnloadDependents: {
      bool finished = true;
      if (unloadComp->flags & RendUnloadFlags_UnloadDependents) {
        dynarray_for_t(&resComp->dependents, EcsEntityId, dependent) {
          if (ecs_world_has_t(world, *dependent, RendResComp)) {
            ecs_utils_maybe_add_t(world, *dependent, RendResUnloadComp);
            finished = false;
          }
        }
      }
      if (finished) {
        ++unloadComp->state;
      }
    } break;
    case RendResUnloadState_UnregisterDependencies: {
      dynarray_for_t(&resComp->dependencies, EcsEntityId, dependency) {
        if (ecs_view_contains(otherResView, *dependency)) {
          ecs_view_jump(otherResItr, *dependency);
          RendResComp* dependencyRes = ecs_view_write_t(otherResItr, RendResComp);
          rend_res_remove_dependent(dependencyRes, *dependency);
        }
      }

      const RendResGlobalDef* globalDef = rend_res_global_lookup(asset_id(assetComp));
      if (globalDef) {
        // Resource had a global definition; unregister it from the repository.
        diag_assert(!globalDef->ignoreAssetChanges);
        rvk_repository_unset(device->repository, globalDef->repoId);
      }

      ++unloadComp->state;
    } break;
    case RendResUnloadState_Destroy: {
      rend_res_teardown(world, resComp, entity);
      ++unloadComp->state;
    } break;
    case RendResUnloadState_Done: {
    } break;
    }
  }
}

ecs_module_init(rend_resource_module) {
  ecs_register_comp(
      RendResGraphicComp, .destructor = ecs_destruct_graphic_comp, .destructOrder = 1);
  ecs_register_comp(RendResShaderComp, .destructor = ecs_destruct_shader_comp, .destructOrder = 2);
  ecs_register_comp(RendResMeshComp, .destructor = ecs_destruct_mesh_comp, .destructOrder = 3);
  ecs_register_comp(
      RendResTextureComp, .destructor = ecs_destruct_texture_comp, .destructOrder = 4);
  ecs_register_comp(
      RendResComp, .destructor = ecs_destruct_res_comp, .combinator = ecs_combine_resource);
  ecs_register_comp_empty(RendResDebugComp);
  ecs_register_comp_empty(RendResFinishedComp);
  ecs_register_comp(RendResUnloadComp, .combinator = ecs_combine_resource_unload);
  ecs_register_comp_empty(RendResGlobalInitializedComp);
  ecs_register_comp_empty(RendResGlobalComp);

  ecs_register_view(PlatReadView);
  ecs_register_view(ResWriteView);
  ecs_register_view(ShaderReadView);
  ecs_register_view(MeshReadView);
  ecs_register_view(TextureReadView);

  ecs_register_system(
      RendGlobalResourceUpdateSys,
      ecs_register_view(GlobalResourceInitView),
      ecs_register_view(GlobalResourceRequestView));

  ecs_register_system(
      RendResLoadSys,
      ecs_view_id(PlatReadView),
      ecs_register_view(ResLoadView),
      ecs_register_view(ResLoadDependencyView),
      ecs_view_id(ShaderReadView),
      ecs_view_id(MeshReadView),
      ecs_view_id(TextureReadView));

  ecs_register_system(RendResUnloadUnusedSys, ecs_register_view(ResUnloadUnusedView));
  ecs_register_system(RendResUnloadChangedSys, ecs_register_view(UnloadChangedView));

  ecs_register_system(
      RendResUnloadUpdateSys,
      ecs_view_id(PlatReadView),
      ecs_register_view(UnloadUpdateView),
      ecs_view_id(ResWriteView));

  ecs_order(RendResLoadSys, RendOrder_ResourceLoad);
  ecs_order(RendResUnloadUnusedSys, RendOrder_Draw + 1);
}

bool rend_res_is_loading(const RendResComp* comp) {
  return comp->state < RendResLoadState_FinishedSuccess;
}

bool rend_res_is_failed(const RendResComp* comp) {
  return comp->state == RendResLoadState_FinishedFailure;
}

bool rend_res_is_unused(const RendResComp* comp) {
  // NOTE: Checking for at least 1 tick of being unused to avoid depending on system order.
  return comp->unusedTicks > 1;
}

bool rend_res_is_persistent(const RendResComp* comp) {
  return (comp->flags & RendResFlags_Persistent) != 0;
}

u32 rend_res_ticks_until_unload(const RendResComp* comp) {
  if (comp->unusedTicks > rend_res_unload_delay) {
    return 0;
  }
  return rend_res_unload_delay - comp->unusedTicks;
}

u32 rend_res_dependents(const RendResComp* comp) { return (u32)comp->dependents.size; }

const RendReport* rend_res_graphic_report(const RendResGraphicComp* comp) { return comp->report; }

u32 rend_res_mesh_vertices(const RendResMeshComp* comp) { return comp->mesh->vertexCount; }

u32 rend_res_mesh_indices(const RendResMeshComp* comp) { return comp->mesh->indexCount; }

usize rend_res_mesh_memory(const RendResMeshComp* comp) {
  return comp->mesh->vertexBuffer.size + comp->mesh->indexBuffer.size;
}

GeoBox rend_res_mesh_bounds(const RendResMeshComp* comp) { return comp->mesh->bounds; }

u16 rend_res_texture_width(const RendResTextureComp* comp) {
  return comp->texture->image.size.width;
}

u16 rend_res_texture_height(const RendResTextureComp* comp) {
  return comp->texture->image.size.height;
}

u16 rend_res_texture_layers(const RendResTextureComp* comp) { return comp->texture->image.layers; }

u8 rend_res_texture_mip_levels(const RendResTextureComp* comp) {
  return comp->texture->image.mipLevels;
}

bool rend_res_texture_is_cube(const RendResTextureComp* comp) {
  return comp->texture->image.type == RvkImageType_ColorSourceCube;
}

String rend_res_texture_format_str(const RendResTextureComp* comp) {
  return vkFormatStr(comp->texture->image.vkFormat);
}

usize rend_res_texture_memory(const RendResTextureComp* comp) {
  return comp->texture->image.mem.size;
}

AssetGraphicPass rend_res_pass(const RendResGraphicComp* comp) { return comp->graphic->passId; }
i32 rend_res_pass_order(const RendResGraphicComp* comp) { return comp->graphic->passOrder; }

bool rend_res_debug_get(EcsWorld* world, const EcsEntityId resource) {
  return ecs_world_has_t(world, resource, RendResDebugComp);
}

void rend_res_debug_set(EcsWorld* world, const EcsEntityId resource, const bool value) {
  if (!value && ecs_world_has_t(world, resource, RendResDebugComp)) {
    ecs_world_remove_t(world, resource, RendResDebugComp);
  } else if (value && !ecs_world_has_t(world, resource, RendResDebugComp)) {
    ecs_world_add_empty_t(world, resource, RendResDebugComp);
  }
}

bool rend_res_request(EcsWorld* world, const EcsEntityId assetEntity) {
  return rend_res_request_internal(world, assetEntity, RendResFlags_None);
}

void rend_res_mark_used(RendResComp* resComp) { resComp->flags |= RendResFlags_Used; }

void rend_res_teardown(EcsWorld* world, const RendResComp* res, const EcsEntityId entity) {
  if (res->state > RendResLoadState_AssetAcquire && res->state < RendResLoadState_FinishedSuccess) {
    asset_release(world, entity);
  }
  ecs_world_remove_t(world, entity, RendResComp);
  ecs_utils_maybe_remove_t(world, entity, RendResUnloadComp);
  ecs_utils_maybe_remove_t(world, entity, RendResFinishedComp);
  ecs_utils_maybe_remove_t(world, entity, RendResGraphicComp);
  ecs_utils_maybe_remove_t(world, entity, RendResShaderComp);
  ecs_utils_maybe_remove_t(world, entity, RendResMeshComp);
  ecs_utils_maybe_remove_t(world, entity, RendResTextureComp);
}
