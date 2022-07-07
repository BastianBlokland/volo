#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_register.h"

#include "platform_internal.h"
#include "reset_internal.h"
#include "resource_internal.h"
#include "rvk/device_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/mesh_internal.h"
#include "rvk/repository_internal.h"
#include "rvk/shader_internal.h"
#include "rvk/texture_internal.h"

static const u32 g_rendResUnloadUnusedAfterTicks = 480; // NOTE: Less then 2 is not supported.

typedef struct {
  RvkRepositoryId repoId;
  String          assetId;
  bool            reloadable;
} RendResGlobalDef;

static const RendResGlobalDef g_rendResGlobal[] = {
    {
        .repoId  = RvkRepositoryId_MissingTexture,
        .assetId = string_static("textures/missing.ptx"),
    },
    {
        .repoId  = RvkRepositoryId_MissingTextureCube,
        .assetId = string_static("textures/missing_cube.atx"),
    },
    {
        .repoId     = RvkRepositoryId_WireframeGraphic,
        .assetId    = string_static("graphics/wireframe.gra"),
        .reloadable = true,
    },
    {
        .repoId     = RvkRepositoryId_WireframeSkinnedGraphic,
        .assetId    = string_static("graphics/wireframe_skinned.gra"),
        .reloadable = true,
    },
    {
        .repoId     = RvkRepositoryId_DebugSkinningGraphic,
        .assetId    = string_static("graphics/debug/debug_skinning.gra"),
        .reloadable = true,
    },
};

ecs_comp_define_public(RendResGraphicComp);
ecs_comp_define_public(RendResShaderComp);
ecs_comp_define_public(RendResMeshComp);
ecs_comp_define_public(RendResTextureComp);

typedef enum {
  RendResFlags_None               = 0,
  RendResFlags_Used               = 1 << 0,
  RendResFlags_Persistent         = 1 << 1, // Always considered in-use.
  RendResFlags_IgnoreAssetChanges = 1 << 2, // Don't unload when the backend asset changes.
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
  RendResLoadState state;
  RendResFlags     flags;
  u64              unusedTicks;
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
  rvk_graphic_destroy(comp->graphic);
}

static void ecs_destruct_shader_comp(void* data) {
  RendResShaderComp* comp = data;
  rvk_shader_destroy(comp->shader);
}

static void ecs_destruct_mesh_comp(void* data) {
  RendResMeshComp* comp = data;
  rvk_mesh_destroy(comp->mesh);
}

static void ecs_destruct_texture_comp(void* data) {
  RendResTextureComp* comp = data;
  rvk_texture_destroy(comp->texture);
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

ecs_view_define(ShaderWriteView) {
  ecs_access_with(RendResComp);
  ecs_access_write(RendResShaderComp);
}

ecs_view_define(MeshWriteView) {
  ecs_access_with(RendResComp);
  ecs_access_write(RendResMeshComp);
}

ecs_view_define(TextureWriteView) {
  ecs_access_with(RendResComp);
  ecs_access_write(RendResTextureComp);
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
      .dependencies = dynarray_create_t(g_alloc_heap, EcsEntityId, 0),
      .dependents   = dynarray_create_t(g_alloc_heap, EcsEntityId, 0));
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
    if (!def->reloadable) {
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
  return true;
}

static bool rend_res_dependencies_acquire(EcsWorld* world, EcsIterator* resourceItr) {
  RendResComp*            resComp           = ecs_view_write_t(resourceItr, RendResComp);
  const AssetGraphicComp* maybeAssetGraphic = ecs_view_read_t(resourceItr, AssetGraphicComp);
  if (maybeAssetGraphic) {
    const RendResFlags depFlags = resComp->flags; // Transfer the flags down to the dependencies.

    array_ptr_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
      rend_res_request_internal(world, ptr->shader, depFlags);
      rend_res_add_dependency(resComp, ptr->shader);
    }

    if (maybeAssetGraphic->mesh) {
      rend_res_request_internal(world, maybeAssetGraphic->mesh, depFlags);
      rend_res_add_dependency(resComp, maybeAssetGraphic->mesh);
    }

    array_ptr_for_t(maybeAssetGraphic->samplers, AssetGraphicSampler, ptr) {
      rend_res_request_internal(world, ptr->texture, depFlags);
      rend_res_add_dependency(resComp, ptr->texture);
    }
  }
  return true;
}

static bool rend_res_dependencies_wait(EcsWorld* world, EcsIterator* resourceItr) {
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

    if (dependencyRes->state == RendResLoadState_FinishedFailure) {
      // Dependency failed to load, also fail this resource.
      resComp->state = RendResLoadState_FinishedFailure;
      return false;
    }
    if (dependencyRes->state != RendResLoadState_FinishedSuccess) {
      ready = false;
      continue;
    }
  }
  return ready;
}

static bool rend_res_create(RvkDevice* dev, EcsWorld* world, EcsIterator* resourceItr) {
  const EcsEntityId       entity            = ecs_view_entity(resourceItr);
  const String            id                = asset_id(ecs_view_read_t(resourceItr, AssetComp));
  RendResComp*            resComp           = ecs_view_write_t(resourceItr, RendResComp);
  const AssetGraphicComp* maybeAssetGraphic = ecs_view_read_t(resourceItr, AssetGraphicComp);
  const AssetShaderComp*  maybeAssetShader  = ecs_view_read_t(resourceItr, AssetShaderComp);
  const AssetMeshComp*    maybeAssetMesh    = ecs_view_read_t(resourceItr, AssetMeshComp);
  const AssetTextureComp* maybeAssetTexture = ecs_view_read_t(resourceItr, AssetTextureComp);

  if (maybeAssetGraphic) {
    RvkGraphic* graphic = rvk_graphic_create(dev, maybeAssetGraphic, id);
    ecs_world_add_t(world, entity, RendResGraphicComp, .graphic = graphic);

    // Add shaders.
    EcsView* shaderView = ecs_world_view_t(world, ShaderWriteView);
    array_ptr_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
      if (!ecs_view_contains(shaderView, ptr->shader)) {
        log_e("Invalid shader reference", log_param("graphic", fmt_text(id)));
        resComp->state = RendResLoadState_FinishedFailure;
        return false;
      }
      EcsIterator*       shaderItr  = ecs_view_at(shaderView, ptr->shader);
      RendResShaderComp* shaderComp = ecs_view_write_t(shaderItr, RendResShaderComp);
      rvk_graphic_shader_add(
          graphic, shaderComp->shader, ptr->overrides.values, ptr->overrides.count);
    }

    // Add mesh.
    if (maybeAssetGraphic->mesh) {
      EcsView* meshView = ecs_world_view_t(world, MeshWriteView);
      if (!ecs_view_contains(meshView, maybeAssetGraphic->mesh)) {
        log_e("Invalid mesh reference", log_param("graphic", fmt_text(id)));
        resComp->state = RendResLoadState_FinishedFailure;
        return false;
      }
      EcsIterator*     meshItr  = ecs_view_at(meshView, maybeAssetGraphic->mesh);
      RendResMeshComp* meshComp = ecs_view_write_t(meshItr, RendResMeshComp);
      rvk_graphic_mesh_add(graphic, meshComp->mesh);
    }

    // Add samplers.
    EcsView* textureView = ecs_world_view_t(world, TextureWriteView);
    array_ptr_for_t(maybeAssetGraphic->samplers, AssetGraphicSampler, ptr) {
      if (!ecs_view_contains(textureView, ptr->texture)) {
        log_e("Invalid texture reference", log_param("graphic", fmt_text(id)));
        resComp->state = RendResLoadState_FinishedFailure;
        return false;
      }
      EcsIterator*        textureItr  = ecs_view_at(textureView, ptr->texture);
      RendResTextureComp* textureComp = ecs_view_write_t(textureItr, RendResTextureComp);
      rvk_graphic_sampler_add(graphic, textureComp->texture, ptr);
    }

    const RendResGlobalDef* globalDef = rend_res_global_lookup(id);
    if (globalDef) {
      rvk_repository_graphic_set(dev->repository, globalDef->repoId, graphic);
    }
    return true;
  }

  if (maybeAssetShader) {
    ecs_world_add_t(
        world, entity, RendResShaderComp, .shader = rvk_shader_create(dev, maybeAssetShader, id));
    return true;
  }

  if (maybeAssetMesh) {
    RvkMesh* mesh = rvk_mesh_create(dev, maybeAssetMesh, id);
    ecs_world_add_t(world, entity, RendResMeshComp, .mesh = mesh);
    return true;
  }

  if (maybeAssetTexture) {
    RvkTexture* tex = rvk_texture_create(dev, maybeAssetTexture, id);
    ecs_world_add_t(world, entity, RendResTextureComp, .texture = tex);

    const RendResGlobalDef* globalDef = rend_res_global_lookup(id);
    if (globalDef) {
      rvk_repository_texture_set(dev->repository, globalDef->repoId, tex);
    }
    return true;
  }

  log_e("Unsupported render resource asset type", log_param("id", fmt_text(id)));
  resComp->state = RendResLoadState_FinishedFailure;
  return false;
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
  /**
   * NOTE: We're getting a mutable RvkDevice pointer from a read-access on RendPlatformComp. This
   * means we have to make sure that all api's we use from RvkDevice are actually thread-safe.
   */
  RvkDevice* device = platform->device;

  EcsView* resourceView = ecs_world_view_t(world, ResLoadView);
  for (EcsIterator* itr = ecs_view_itr(resourceView); ecs_view_walk(itr);) {
    RendResComp* resComp = ecs_view_write_t(itr, RendResComp);
    switch (resComp->state) {
    case RendResLoadState_AssetAcquire: {
      if (rend_res_asset_acquire(world, itr)) {
        ++resComp->state;
      }
    } break;
    case RendResLoadState_AssetWait: {
      if (rend_res_asset_wait(world, itr)) {
        ++resComp->state;
      }
    } break;
    case RendResLoadState_DependenciesAcquire: {
      if (rend_res_dependencies_acquire(world, itr)) {
        ++resComp->state;
      }
    } break;
    case RendResLoadState_DependenciesWait: {
      if (rend_res_dependencies_wait(world, itr)) {
        ++resComp->state;
      }
    } break;
    case RendResLoadState_Create: {
      if (rend_res_create(device, world, itr)) {
        ++resComp->state;
      }
    } break;
    case RendResLoadState_FinishedSuccess: {
      rend_res_finished_success(world, itr);
    } break;
    case RendResLoadState_FinishedFailure: {
      rend_res_finished_failure(world, itr);
    } break;
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
    if (resComp->unusedTicks++ > g_rendResUnloadUnusedAfterTicks) {
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
 * Start unloading resources where the source asset has changed.
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
          if (ecs_world_has_t(world, *dependent, RendResFinishedComp)) {
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
        diag_assert(globalDef->reloadable); // Only reloadable global assets can be unloaded.
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
  ecs_register_comp_empty(RendResFinishedComp);
  ecs_register_comp(RendResUnloadComp, .combinator = ecs_combine_resource_unload);
  ecs_register_comp_empty(RendResGlobalInitializedComp);
  ecs_register_comp_empty(RendResGlobalComp);

  ecs_register_view(PlatReadView);
  ecs_register_view(ResWriteView);
  ecs_register_view(ShaderWriteView);
  ecs_register_view(MeshWriteView);
  ecs_register_view(TextureWriteView);

  ecs_register_system(
      RendGlobalResourceUpdateSys,
      ecs_register_view(GlobalResourceInitView),
      ecs_register_view(GlobalResourceRequestView));

  ecs_register_system(
      RendResLoadSys,
      ecs_view_id(PlatReadView),
      ecs_register_view(ResLoadView),
      ecs_register_view(ResLoadDependencyView),
      ecs_view_id(ResWriteView),
      ecs_view_id(ShaderWriteView),
      ecs_view_id(MeshWriteView),
      ecs_view_id(TextureWriteView));

  ecs_register_system(RendResUnloadUnusedSys, ecs_register_view(ResUnloadUnusedView));
  ecs_register_system(RendResUnloadChangedSys, ecs_register_view(UnloadChangedView));

  ecs_register_system(
      RendResUnloadUpdateSys,
      ecs_view_id(PlatReadView),
      ecs_register_view(UnloadUpdateView),
      ecs_view_id(ResWriteView));

  ecs_order(RendResUnloadUnusedSys, RendOrder_DrawExecute + 1);
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

u64 rend_res_ticks_until_unload(const RendResComp* comp) {
  if (comp->unusedTicks > g_rendResUnloadUnusedAfterTicks) {
    return 0;
  }
  return g_rendResUnloadUnusedAfterTicks - comp->unusedTicks;
}

usize rend_res_mesh_data_size(const RendResMeshComp* comp) {
  return comp->mesh->vertexBuffer.size + comp->mesh->indexBuffer.size;
}

usize rend_res_texture_data_size(const RendResTextureComp* comp) {
  return comp->texture->image.mem.size;
}

i32 rend_res_render_order(const RendResGraphicComp* comp) { return comp->graphic->renderOrder; }

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
