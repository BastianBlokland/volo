#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "rend_instance.h"

#include "platform_internal.h"
#include "resource_internal.h"
#include "rvk/device_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/mesh_internal.h"
#include "rvk/repository_internal.h"
#include "rvk/shader_internal.h"
#include "rvk/texture_internal.h"

ecs_comp_define_public(RendResGraphicComp);
ecs_comp_define_public(RendResShaderComp);
ecs_comp_define_public(RendResMeshComp);
ecs_comp_define_public(RendResTextureComp);

ecs_comp_define(RendGlobalResComp) { EcsEntityId missingTex; };
ecs_comp_define(RendGlobalResLoadedComp);

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
  RendResUnloadState_Wait,
  RendResUnloadState_Destroy,
  RendResUnloadState_Done,
} RendResUnloadState;

ecs_comp_define(RendResComp) {
  RendResLoadState state;
  DynArray         dependencies; // EcsEntityId[]
};
ecs_comp_define(RendResLoadedComp);
ecs_comp_define(RendResFailedComp);
ecs_comp_define(RendResUnloadComp) { RendResUnloadState state; };

static void ecs_destruct_graphic_comp(void* data) {
  RendResGraphicComp* comp = data;
  if (comp->graphic) {
    rvk_graphic_destroy(comp->graphic);
  }
}

static void ecs_destruct_shader_comp(void* data) {
  RendResShaderComp* comp = data;
  if (comp->shader) {
    rvk_shader_destroy(comp->shader);
  }
}

static void ecs_destruct_mesh_comp(void* data) {
  RendResMeshComp* comp = data;
  if (comp->mesh) {
    rvk_mesh_destroy(comp->mesh);
  }
}

static void ecs_destruct_texture_comp(void* data) {
  RendResTextureComp* comp = data;
  if (comp->texture) {
    rvk_texture_destroy(comp->texture);
  }
}

static void ecs_destruct_res_comp(void* data) {
  RendResComp* comp = data;
  dynarray_destroy(&comp->dependencies);
}

static void rend_resource_add_dep(RendResComp* res, EcsEntityId dependency) {
  if (!dynarray_search_linear(&res->dependencies, ecs_compare_entity, &dependency)) {
    *dynarray_push_t(&res->dependencies, EcsEntityId) = dependency;
  }
}

static void ecs_combine_resource(void* dataA, void* dataB) {
  RendResComp* compA = dataA;
  RendResComp* compB = dataB;
  compA->state       = math_max(compA->state, compB->state);

  dynarray_for_t(&compB->dependencies, EcsEntityId, entity) {
    rend_resource_add_dep(compA, *entity);
  }
  dynarray_destroy(&compB->dependencies);
}

ecs_view_define(RendPlatView) { ecs_access_read(RendPlatformComp); }

ecs_view_define(ShaderView) { ecs_access_write(RendResShaderComp); }
ecs_view_define(GraphicView) { ecs_access_write(RendResGraphicComp); }
ecs_view_define(MeshView) { ecs_access_write(RendResMeshComp); }
ecs_view_define(TextureView) { ecs_access_write(RendResTextureComp); }

static void rend_resource_request(EcsWorld* world, EcsEntityId entity) {
  if (!ecs_world_has_t(world, entity, RendResComp)) {
    ecs_world_add_t(
        world,
        entity,
        RendResComp,
        .dependencies = dynarray_create_t(g_alloc_heap, EcsEntityId, 0));
  }
}

static EcsEntityId rend_resource_request_asset(EcsWorld* world, AssetManagerComp* man, String id) {
  const EcsEntityId assetEntity = asset_lookup(world, man, id);
  rend_resource_request(world, assetEntity);
  return assetEntity;
}

static bool rend_resource_set_wellknown_texture(
    EcsWorld* world, RendPlatformComp* plat, const RvkRepositoryId id, const EcsEntityId entity) {

  if (ecs_world_has_t(world, entity, RendResLoadedComp)) {
    RendResTextureComp* comp = ecs_utils_write_t(world, TextureView, entity, RendResTextureComp);
    rvk_repository_texture_set(plat->device->repository, id, comp->texture);
    return true;
  }
  return false;
}

ecs_view_define(GlobalResourceUpdateView) {
  ecs_access_write(RendPlatformComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_write(RendGlobalResComp);
  ecs_access_without(RendGlobalResLoadedComp);
}

/**
 * Load all global resources.
 */
ecs_system_define(RendGlobalResourceLoadSys) {
  EcsIterator* globalItr = ecs_view_first(ecs_world_view_t(world, GlobalResourceUpdateView));
  if (!globalItr) {
    return;
  }
  RendPlatformComp*  plat     = ecs_view_write_t(globalItr, RendPlatformComp);
  AssetManagerComp*  assetMan = ecs_view_write_t(globalItr, AssetManagerComp);
  RendGlobalResComp* resComp  = ecs_view_write_t(globalItr, RendGlobalResComp);

  if (!resComp) {
    const EcsEntityId missingTex =
        rend_resource_request_asset(world, assetMan, string_lit("textures/missing.ppm"));
    resComp = ecs_world_add_t(
        world, ecs_view_entity(globalItr), RendGlobalResComp, .missingTex = missingTex);
  }

  // Wait for all global resources to be loaded.
  if (!rend_resource_set_wellknown_texture(
          world, plat, RvkRepositoryId_MissingTexture, resComp->missingTex)) {
    return;
  }

  // Global resources load finished.
  ecs_world_add_empty_t(world, ecs_view_entity(globalItr), RendGlobalResLoadedComp);
}

ecs_view_define(RequestForInstanceView) { ecs_access_read(RendInstanceComp); };

/**
 * Request the graphic to be loaded for all entities with a RendInstanceComp.
 */
ecs_system_define(RendResRequestSys) {
  EcsView* instanceView = ecs_world_view_t(world, RequestForInstanceView);
  for (EcsIterator* itr = ecs_view_itr(instanceView); ecs_view_walk(itr);) {
    const RendInstanceComp* comp = ecs_view_read_t(itr, RendInstanceComp);
    rend_resource_request(world, comp->graphic);
  }
}

ecs_view_define(RendResLoadView) {
  ecs_access_without(RendResLoadedComp);
  ecs_access_without(RendResFailedComp);
  ecs_access_read(AssetComp);
  ecs_access_write(RendResComp);

  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_maybe_read(AssetShaderComp);
  ecs_access_maybe_read(AssetMeshComp);
  ecs_access_maybe_read(AssetTextureComp);
}

static void rend_resource_fail(EcsWorld* world, EcsIterator* resourceItr) {
  ecs_view_write_t(resourceItr, RendResComp)->state = RendResLoadState_FinishedFailure;
  ecs_world_add_empty_t(world, ecs_view_entity(resourceItr), RendResFailedComp);
}

static void rend_resource_load(RvkDevice* dev, EcsWorld* world, EcsIterator* resourceItr) {
  const EcsEntityId       entity            = ecs_view_entity(resourceItr);
  RendResComp*            resComp           = ecs_view_write_t(resourceItr, RendResComp);
  const String            id                = asset_id(ecs_view_read_t(resourceItr, AssetComp));
  const AssetGraphicComp* maybeAssetGraphic = ecs_view_read_t(resourceItr, AssetGraphicComp);
  const AssetShaderComp*  maybeAssetShader  = ecs_view_read_t(resourceItr, AssetShaderComp);
  const AssetMeshComp*    maybeAssetMesh    = ecs_view_read_t(resourceItr, AssetMeshComp);
  const AssetTextureComp* maybeAssetTexture = ecs_view_read_t(resourceItr, AssetTextureComp);

  switch (resComp->state) {
  case RendResLoadState_AssetAcquire:
    asset_acquire(world, entity);
    break;
  case RendResLoadState_AssetWait:
    if (ecs_world_has_t(world, entity, AssetFailedComp)) {
      rend_resource_fail(world, resourceItr);
      asset_release(world, entity);
      return;
    }
    if (!ecs_world_has_t(world, entity, AssetLoadedComp)) {
      return;
    }
    break;
  case RendResLoadState_DependenciesAcquire:
    if (maybeAssetGraphic) {
      // Shaders.
      array_ptr_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
        rend_resource_request(world, ptr->shader);
        rend_resource_add_dep(resComp, ptr->shader);
      }
      // Mesh.
      if (maybeAssetGraphic->mesh) {
        rend_resource_request(world, maybeAssetGraphic->mesh);
        rend_resource_add_dep(resComp, maybeAssetGraphic->mesh);
      }
      // Textures.
      array_ptr_for_t(maybeAssetGraphic->samplers, AssetGraphicSampler, ptr) {
        rend_resource_request(world, ptr->texture);
        rend_resource_add_dep(resComp, ptr->texture);
      }
    }
    break;
  case RendResLoadState_DependenciesWait:
    dynarray_for_t(&resComp->dependencies, EcsEntityId, dep) {
      if (ecs_world_has_t(world, *dep, RendResFailedComp)) {
        // Dependency failed to load, also fail this resource.
        rend_resource_fail(world, resourceItr);
        asset_release(world, entity);
        return;
      }
      if (!ecs_world_has_t(world, *dep, RendResLoadedComp)) {
        return; // Dependency not ready yet.
      }
    }
    break;
  case RendResLoadState_Create: {
    if (maybeAssetGraphic) {
      RendResGraphicComp* graphicComp = ecs_world_add_t(
          world,
          entity,
          RendResGraphicComp,
          .graphic = rvk_graphic_create(dev, maybeAssetGraphic, id));

      // Add shaders.
      array_ptr_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
        RendResShaderComp* comp =
            ecs_utils_write_t(world, ShaderView, ptr->shader, RendResShaderComp);
        rvk_graphic_shader_add(
            graphicComp->graphic, comp->shader, ptr->overrides.values, ptr->overrides.count);
      }

      // Add mesh.
      if (maybeAssetGraphic->mesh) {
        RendResMeshComp* meshComp =
            ecs_utils_write_t(world, MeshView, maybeAssetGraphic->mesh, RendResMeshComp);
        rvk_graphic_mesh_add(graphicComp->graphic, meshComp->mesh);
      }

      // Add samplers.
      array_ptr_for_t(maybeAssetGraphic->samplers, AssetGraphicSampler, ptr) {
        RendResTextureComp* comp =
            ecs_utils_write_t(world, TextureView, ptr->texture, RendResTextureComp);
        rvk_graphic_sampler_add(graphicComp->graphic, comp->texture, ptr);
      }
    } else if (maybeAssetShader) {
      ecs_world_add_t(
          world, entity, RendResShaderComp, .shader = rvk_shader_create(dev, maybeAssetShader, id));
    } else if (maybeAssetMesh) {
      ecs_world_add_t(
          world, entity, RendResMeshComp, .mesh = rvk_mesh_create(dev, maybeAssetMesh, id));
    } else if (maybeAssetTexture) {
      ecs_world_add_t(
          world,
          entity,
          RendResTextureComp,
          .texture = rvk_texture_create(dev, maybeAssetTexture, id));
    } else {
      diag_crash_msg("Unsupported resource asset type");
    }
    asset_release(world, entity);
    ecs_world_add_empty_t(world, entity, RendResLoadedComp);
  }
  case RendResLoadState_FinishedSuccess:
  case RendResLoadState_FinishedFailure:
    break;
  }
  ++resComp->state;
}

/**
 * Update all active resource loads.
 */
ecs_system_define(RendResLoadSys) {
  EcsView*     globalView = ecs_world_view_t(world, RendPlatView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const RendPlatformComp* plat   = ecs_view_read_t(globalItr, RendPlatformComp);
  RvkDevice*              device = plat->device;
  /**
   * NOTE: We're getting a mutable RvkDevice pointer from a read-access on RendPlatformComp. This
   * means we have to make sure that all api's we use from RvkDevice are actually thread-safe.
   */
  EcsView* resourceView = ecs_world_view_t(world, RendResLoadView);
  for (EcsIterator* itr = ecs_view_itr(resourceView); ecs_view_walk(itr);) {
    rend_resource_load(device, world, itr);
  }
}

ecs_view_define(RendResUnloadChangedRequestView) {
  ecs_access_with(RendResComp);
  ecs_access_with(AssetChangedComp);
  ecs_access_without(RendResUnloadComp);
}

/**
 * Request resources where the source asset has changed to be unloaded.
 */
ecs_system_define(RendResUnloadChangedRequestSys) {
  EcsView* changedAssetsView = ecs_world_view_t(world, RendResUnloadChangedRequestView);
  for (EcsIterator* itr = ecs_view_itr(changedAssetsView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);

    /**
     * Wait with unloading until the asset has finished loaded to avoid the complexity of unloading
     * a half-loaded asset.
     */
    const bool loaded = ecs_world_has_t(world, entity, RendResLoadedComp) ||
                        ecs_world_has_t(world, entity, RendResFailedComp);
    if (loaded) {
      ecs_world_add_t(world, entity, RendResUnloadComp);
    }
  }
}

ecs_view_define(RendResUnloadUpdateView) { ecs_access_write(RendResUnloadComp); }

/**
 * Update all active resource unloads.
 */
ecs_system_define(RendResUnloadUpdateSys) {
  EcsView* unloadView = ecs_world_view_t(world, RendResUnloadUpdateView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity     = ecs_view_entity(itr);
    RendResUnloadComp* unloadComp = ecs_view_write_t(itr, RendResUnloadComp);
    switch (unloadComp->state) {
    case RendResUnloadState_Wait:
      // Wait for the renderer to stop using this resource.
      break;
    case RendResUnloadState_Destroy:
      ecs_world_remove_t(world, entity, RendResComp);
      ecs_world_remove_t(world, entity, RendResUnloadComp);
      ecs_utils_maybe_remove_t(world, entity, RendResLoadedComp);
      ecs_utils_maybe_remove_t(world, entity, RendResFailedComp);
      ecs_utils_maybe_remove_t(world, entity, RendResGraphicComp);
      ecs_utils_maybe_remove_t(world, entity, RendResShaderComp);
      ecs_utils_maybe_remove_t(world, entity, RendResMeshComp);
      ecs_utils_maybe_remove_t(world, entity, RendResTextureComp);
      break;
    case RendResUnloadState_Done:
      break;
    }
    ++unloadComp->state;
  }
}

ecs_module_init(rend_resource_module) {
  ecs_register_comp(RendResGraphicComp, .destructor = ecs_destruct_graphic_comp);
  ecs_register_comp(RendResShaderComp, .destructor = ecs_destruct_shader_comp);
  ecs_register_comp(RendResMeshComp, .destructor = ecs_destruct_mesh_comp);
  ecs_register_comp(RendResTextureComp, .destructor = ecs_destruct_texture_comp);
  ecs_register_comp(RendGlobalResComp);
  ecs_register_comp_empty(RendGlobalResLoadedComp);
  ecs_register_comp(
      RendResComp, .combinator = ecs_combine_resource, .destructor = ecs_destruct_res_comp);
  ecs_register_comp_empty(RendResLoadedComp);
  ecs_register_comp_empty(RendResFailedComp);
  ecs_register_comp(RendResUnloadComp);

  ecs_register_view(RendPlatView);
  ecs_register_view(ShaderView);
  ecs_register_view(GraphicView);
  ecs_register_view(MeshView);
  ecs_register_view(TextureView);

  ecs_register_system(
      RendGlobalResourceLoadSys,
      ecs_register_view(GlobalResourceUpdateView),
      ecs_view_id(TextureView));

  ecs_register_system(RendResRequestSys, ecs_register_view(RequestForInstanceView));

  ecs_register_system(
      RendResLoadSys,
      ecs_view_id(RendPlatView),
      ecs_register_view(RendResLoadView),
      ecs_view_id(ShaderView),
      ecs_view_id(MeshView),
      ecs_view_id(TextureView));

  ecs_register_system(
      RendResUnloadChangedRequestSys, ecs_register_view(RendResUnloadChangedRequestView));

  ecs_register_system(RendResUnloadUpdateSys, ecs_register_view(RendResUnloadUpdateView));
}

void rend_resource_teardown(EcsWorld* world) {
  const RendPlatformComp* plat = ecs_utils_read_first_t(world, RendPlatView, RendPlatformComp);
  if (plat) {
    // Wait for all rendering to be done.
    rvk_device_wait_idle(plat->device);
  }

  // Teardown graphics.
  EcsView* graphicView = ecs_world_view_t(world, GraphicView);
  for (EcsIterator* itr = ecs_view_itr(graphicView); ecs_view_walk(itr);) {
    RendResGraphicComp* comp = ecs_view_write_t(itr, RendResGraphicComp);
    rvk_graphic_destroy(comp->graphic);
    comp->graphic = null;
  }

  // Teardown shaders.
  EcsView* shaderView = ecs_world_view_t(world, ShaderView);
  for (EcsIterator* itr = ecs_view_itr(shaderView); ecs_view_walk(itr);) {
    RendResShaderComp* comp = ecs_view_write_t(itr, RendResShaderComp);
    rvk_shader_destroy(comp->shader);
    comp->shader = null;
  }

  // Teardown meshes.
  EcsView* meshView = ecs_world_view_t(world, MeshView);
  for (EcsIterator* itr = ecs_view_itr(meshView); ecs_view_walk(itr);) {
    RendResMeshComp* comp = ecs_view_write_t(itr, RendResMeshComp);
    rvk_mesh_destroy(comp->mesh);
    comp->mesh = null;
  }

  // Teardown textures.
  EcsView* textureView = ecs_world_view_t(world, TextureView);
  for (EcsIterator* itr = ecs_view_itr(textureView); ecs_view_walk(itr);) {
    RendResTextureComp* comp = ecs_view_write_t(itr, RendResTextureComp);
    rvk_texture_destroy(comp->texture);
    comp->texture = null;
  }
}
