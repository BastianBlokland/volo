#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
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
  RendResUnloadState_UnloadDependents,
  RendResUnloadState_UnregisterDependencies,
  RendResUnloadState_Destroy,
  RendResUnloadState_Done,
} RendResUnloadState;

ecs_comp_define(RendResComp) {
  RendResLoadState state;
  DynArray         dependencies; // EcsEntityId[], resources this resource depends on.
  DynArray         dependents;   // EcsEntityId[], resources that depend on this resource.
};
ecs_comp_define(RendResFinishedComp);
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
  compA->state       = math_max(compA->state, compB->state);

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

ecs_view_define(RendPlatReadView) { ecs_access_read(RendPlatformComp); }

ecs_view_define(ResourceWriteView) { ecs_access_write(RendResComp); }

ecs_view_define(GraphicWriteView) {
  ecs_access_with(RendResComp);
  ecs_access_write(RendResGraphicComp);
}

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

static void rend_res_request(EcsWorld* world, EcsEntityId entity) {
  if (!ecs_world_has_t(world, entity, RendResComp)) {
    ecs_world_add_t(
        world,
        entity,
        RendResComp,
        .dependencies = dynarray_create_t(g_alloc_heap, EcsEntityId, 0),
        .dependents   = dynarray_create_t(g_alloc_heap, EcsEntityId, 0));
  }
}

static EcsEntityId rend_res_request_asset(EcsWorld* world, AssetManagerComp* man, String id) {
  const EcsEntityId assetEntity = asset_lookup(world, man, id);
  rend_res_request(world, assetEntity);
  return assetEntity;
}

static bool rend_res_set_wellknown_texture(
    EcsWorld* world, RendPlatformComp* plat, const RvkRepositoryId id, const EcsEntityId entity) {

  EcsView* textureView = ecs_world_view_t(world, TextureWriteView);
  if (ecs_view_contains(textureView, entity)) {
    RendResTextureComp* comp =
        ecs_utils_write(textureView, entity, ecs_comp_id(RendResTextureComp));
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
    resComp = ecs_world_add_t(
        world,
        ecs_view_entity(globalItr),
        RendGlobalResComp,
        .missingTex = rend_res_request_asset(world, assetMan, string_lit("textures/missing.ppm")));
  }

  // Wait for all global resources to be loaded.
  if (!rend_res_set_wellknown_texture(
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
    rend_res_request(world, comp->graphic);
  }
}

ecs_view_define(RendResLoadView) {
  ecs_access_without(RendResFinishedComp);
  ecs_access_read(AssetComp);
  ecs_access_write(RendResComp);

  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_maybe_read(AssetShaderComp);
  ecs_access_maybe_read(AssetMeshComp);
  ecs_access_maybe_read(AssetTextureComp);
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

    array_ptr_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
      rend_res_request(world, ptr->shader);
      rend_res_add_dependency(resComp, ptr->shader);
    }

    if (maybeAssetGraphic->mesh) {
      rend_res_request(world, maybeAssetGraphic->mesh);
      rend_res_add_dependency(resComp, maybeAssetGraphic->mesh);
    }

    array_ptr_for_t(maybeAssetGraphic->samplers, AssetGraphicSampler, ptr) {
      rend_res_request(world, ptr->texture);
      rend_res_add_dependency(resComp, ptr->texture);
    }
  }
  return true;
}

static bool rend_res_dependencies_wait(EcsWorld* world, EcsIterator* resourceItr) {
  const EcsEntityId entity         = ecs_view_entity(resourceItr);
  RendResComp*      resComp        = ecs_view_write_t(resourceItr, RendResComp);
  EcsView*          dependencyView = ecs_world_view_t(world, ResourceWriteView);
  EcsIterator*      dependencyItr  = ecs_view_itr(dependencyView);

  dynarray_for_t(&resComp->dependencies, EcsEntityId, dep) {
    if (!ecs_view_contains(dependencyView, *dep)) {
      return false;
    }
    ecs_view_jump(dependencyItr, *dep);
    RendResComp* dependencyRes = ecs_view_write_t(dependencyItr, RendResComp);
    rend_res_add_dependent(dependencyRes, entity);

    if (dependencyRes->state == RendResLoadState_FinishedFailure) {
      // Dependency failed to load, also fail this resource.
      resComp->state = RendResLoadState_FinishedFailure;
      return false;
    }
    if (dependencyRes->state != RendResLoadState_FinishedSuccess) {
      return false;
    }
  }
  return true;
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
    RendResGraphicComp* graphicComp = ecs_world_add_t(
        world,
        entity,
        RendResGraphicComp,
        .graphic = rvk_graphic_create(dev, maybeAssetGraphic, id));

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
          graphicComp->graphic, shaderComp->shader, ptr->overrides.values, ptr->overrides.count);
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
      rvk_graphic_mesh_add(graphicComp->graphic, meshComp->mesh);
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
      rvk_graphic_sampler_add(graphicComp->graphic, textureComp->texture, ptr);
    }
    return true;
  }

  if (maybeAssetShader) {
    ecs_world_add_t(
        world, entity, RendResShaderComp, .shader = rvk_shader_create(dev, maybeAssetShader, id));
    return true;
  }

  if (maybeAssetMesh) {
    ecs_world_add_t(
        world, entity, RendResMeshComp, .mesh = rvk_mesh_create(dev, maybeAssetMesh, id));
    return true;
  }

  if (maybeAssetTexture) {
    ecs_world_add_t(
        world,
        entity,
        RendResTextureComp,
        .texture = rvk_texture_create(dev, maybeAssetTexture, id));
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

/**
 * Update all active resource loads.
 */
ecs_system_define(RendResLoadSys) {
  EcsView*     globalView = ecs_world_view_t(world, RendPlatReadView);
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

ecs_view_define(RendResUnloadChangedRequestView) {
  ecs_access_read(AssetComp);
  ecs_access_with(AssetChangedComp);
  ecs_access_read(RendResComp);
  ecs_access_without(RendResUnloadComp);
}

/**
 * Request resources where the source asset has changed to be unloaded.
 */
ecs_system_define(RendResUnloadChangedRequestSys) {
  EcsView* changedAssetsView = ecs_world_view_t(world, RendResUnloadChangedRequestView);
  for (EcsIterator* itr = ecs_view_itr(changedAssetsView); ecs_view_walk(itr);) {
    const RendResComp* resComp = ecs_view_read_t(itr, RendResComp);
    const String       id      = asset_id(ecs_view_read_t(itr, AssetComp));
    const EcsEntityId  entity  = ecs_view_entity(itr);

    /**
     * Wait with unloading until the asset has finished loaded to avoid the complexity of unloading
     * a half-loaded asset.
     */
    if (resComp->state >= RendResLoadState_FinishedSuccess) {
      log_i("Unloaded changed asset", log_param("id", fmt_text(id)));
      ecs_world_add_t(world, entity, RendResUnloadComp);
    }
  }
}

ecs_view_define(RendResUnloadUpdateView) {
  ecs_access_read(RendResComp);
  ecs_access_write(RendResUnloadComp);
}

/**
 * Update all active resource unloads.
 */
ecs_system_define(RendResUnloadUpdateSys) {
  EcsView* unloadView = ecs_world_view_t(world, RendResUnloadUpdateView);

  EcsView*     resourceView = ecs_world_view_t(world, ResourceWriteView);
  EcsIterator* resourceItr  = ecs_view_itr(resourceView);

  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity     = ecs_view_entity(itr);
    const RendResComp* resComp    = ecs_view_read_t(itr, RendResComp);
    RendResUnloadComp* unloadComp = ecs_view_write_t(itr, RendResUnloadComp);
    switch (unloadComp->state) {
    case RendResUnloadState_UnloadDependents: {
      bool finished = true;
      dynarray_for_t(&resComp->dependents, EcsEntityId, dependent) {
        if (ecs_world_has_t(world, *dependent, RendResComp)) {
          ecs_utils_maybe_add_t(world, *dependent, RendResUnloadComp);
          finished = false;
        }
      }
      if (finished) {
        ++unloadComp->state;
      }
    } break;
    case RendResUnloadState_UnregisterDependencies: {
      dynarray_for_t(&resComp->dependencies, EcsEntityId, dependency) {
        if (ecs_view_contains(resourceView, *dependency)) {
          ecs_view_jump(resourceItr, *dependency);
          RendResComp* dependencyRes = ecs_view_write_t(resourceItr, RendResComp);
          rend_res_remove_dependent(dependencyRes, *dependency);
        }
      }
      ++unloadComp->state;
    } break;
    case RendResUnloadState_Destroy: {
      ecs_world_remove_t(world, entity, RendResComp);
      ecs_world_remove_t(world, entity, RendResUnloadComp);
      ecs_world_remove_t(world, entity, RendResFinishedComp);
      ecs_utils_maybe_remove_t(world, entity, RendResGraphicComp);
      ecs_utils_maybe_remove_t(world, entity, RendResShaderComp);
      ecs_utils_maybe_remove_t(world, entity, RendResMeshComp);
      ecs_utils_maybe_remove_t(world, entity, RendResTextureComp);
      ++unloadComp->state;
    } break;
    case RendResUnloadState_Done: {
    } break;
    }
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
      RendResComp, .destructor = ecs_destruct_res_comp, .combinator = ecs_combine_resource);
  ecs_register_comp_empty(RendResFinishedComp);
  ecs_register_comp(RendResUnloadComp);

  ecs_register_view(RendPlatReadView);
  ecs_register_view(ResourceWriteView);
  ecs_register_view(ShaderWriteView);
  ecs_register_view(GraphicWriteView);
  ecs_register_view(MeshWriteView);
  ecs_register_view(TextureWriteView);

  ecs_register_system(
      RendGlobalResourceLoadSys,
      ecs_register_view(GlobalResourceUpdateView),
      ecs_view_id(TextureWriteView));

  ecs_register_system(RendResRequestSys, ecs_register_view(RequestForInstanceView));

  ecs_register_system(
      RendResLoadSys,
      ecs_view_id(RendPlatReadView),
      ecs_register_view(RendResLoadView),
      ecs_view_id(ResourceWriteView),
      ecs_view_id(ShaderWriteView),
      ecs_view_id(MeshWriteView),
      ecs_view_id(TextureWriteView));

  ecs_register_system(
      RendResUnloadChangedRequestSys, ecs_register_view(RendResUnloadChangedRequestView));

  ecs_register_system(
      RendResUnloadUpdateSys,
      ecs_register_view(RendResUnloadUpdateView),
      ecs_view_id(ResourceWriteView));
}

void rend_resource_teardown(EcsWorld* world) {
  const RendPlatformComp* plat = ecs_utils_read_first_t(world, RendPlatReadView, RendPlatformComp);
  if (plat) {
    // Wait for all rendering to be done.
    rvk_device_wait_idle(plat->device);
  }

  // Teardown graphics.
  EcsView* graphicView = ecs_world_view_t(world, GraphicWriteView);
  for (EcsIterator* itr = ecs_view_itr(graphicView); ecs_view_walk(itr);) {
    RendResGraphicComp* comp = ecs_view_write_t(itr, RendResGraphicComp);
    rvk_graphic_destroy(comp->graphic);
    comp->graphic = null;
  }

  // Teardown shaders.
  EcsView* shaderView = ecs_world_view_t(world, ShaderWriteView);
  for (EcsIterator* itr = ecs_view_itr(shaderView); ecs_view_walk(itr);) {
    RendResShaderComp* comp = ecs_view_write_t(itr, RendResShaderComp);
    rvk_shader_destroy(comp->shader);
    comp->shader = null;
  }

  // Teardown meshes.
  EcsView* meshView = ecs_world_view_t(world, MeshWriteView);
  for (EcsIterator* itr = ecs_view_itr(meshView); ecs_view_walk(itr);) {
    RendResMeshComp* comp = ecs_view_write_t(itr, RendResMeshComp);
    rvk_mesh_destroy(comp->mesh);
    comp->mesh = null;
  }

  // Teardown textures.
  EcsView* textureView = ecs_world_view_t(world, TextureWriteView);
  for (EcsIterator* itr = ecs_view_itr(textureView); ecs_view_walk(itr);) {
    RendResTextureComp* comp = ecs_view_write_t(itr, RendResTextureComp);
    rvk_texture_destroy(comp->texture);
    comp->texture = null;
  }
}
