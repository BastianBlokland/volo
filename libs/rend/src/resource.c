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

ecs_comp_define(RendGlobalResComp) {
  EcsEntityId missingTex;
  EcsEntityId missingMesh;
};
ecs_comp_define(RendGlobalResLoadedComp);

typedef enum {
  RendResLoadState_AcquireAsset,
  RendResLoadState_AcquireDependencies,
  RendResLoadState_Create,
  RendResLoadState_Loaded,
  RendResLoadState_Failed,
} RendResLoadState;

typedef enum {
  RendResUnloadState_Wait,
  RendResUnloadState_Destroy,
  RendResUnloadState_Done,
} RendResUnloadState;

ecs_comp_define(RendResComp) { RendResLoadState state; };
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

static void ecs_combine_resource(void* dataA, void* dataB) {
  RendResComp* compA = dataA;
  RendResComp* compB = dataB;
  compA->state       = math_max(compA->state, compB->state);
}

ecs_view_define(RendPlatView) { ecs_access_read(RendPlatformComp); }

ecs_view_define(PlatformAndGlobalResView) {
  ecs_access_read(RendPlatformComp);
  ecs_access_read(RendGlobalResComp);
}

ecs_view_define(ShaderView) { ecs_access_write(RendResShaderComp); }
ecs_view_define(GraphicView) { ecs_access_write(RendResGraphicComp); }
ecs_view_define(MeshView) { ecs_access_write(RendResMeshComp); }
ecs_view_define(TextureView) { ecs_access_write(RendResTextureComp); }

static EcsEntityId rend_resource_request(EcsWorld* world, AssetManagerComp* assetMan, String id) {
  const EcsEntityId assetEntity = asset_lookup(world, assetMan, id);
  ecs_utils_maybe_add_t(world, assetEntity, RendResComp);
  return assetEntity;
}

static bool rend_resource_set_wellknown_texture(
    EcsWorld* world, RendPlatformComp* plat, const RvkRepositoryId id, const EcsEntityId entity) {

  if (ecs_world_has_t(world, entity, RendResLoadedComp)) {
    RendResTextureComp* comp = ecs_utils_write_t(world, TextureView, entity, RendResTextureComp);
    rvk_repository_texture_set(plat->device->repository, id, comp->texture);
    return true;
  }

  return false; // Texture has not finished loading.
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
        .missingTex  = rend_resource_request(world, assetMan, string_lit("textures/missing.ppm")),
        .missingMesh = rend_resource_request(world, assetMan, string_lit("meshes/missing.obj")));
  }

  /**
   * Wait for all global resources to be loaded.
   */
  if (!rend_resource_set_wellknown_texture(
          world, plat, RvkRepositoryId_MissingTexture, resComp->missingTex)) {
    return;
  }

  /**
   * Global resources load finished.
   */
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
    ecs_utils_maybe_add_t(world, comp->graphic, RendResComp);
  }
}

/**
 * Retrieve the texture entity for a graphic sampler.
 * NOTE: Falls back to the 'missingTexture' if the desired texture failed to load.
 */
static EcsEntityId rend_resource_texture_entity(
    EcsWorld* world, const RendGlobalResComp* res, const AssetGraphicSampler* sampler) {
  return ecs_world_has_t(world, sampler->texture, RendResFailedComp) ? res->missingTex
                                                                     : sampler->texture;
}

/**
 * Retrieve the mesh entity for a graphic.
 * NOTE: Falls back to the 'missingMesh' if the desired mesh failed to load.
 */
static EcsEntityId rend_resource_mesh_entity(
    EcsWorld* world, const RendGlobalResComp* res, const AssetGraphicComp* graphic) {
  return ecs_world_has_t(world, graphic->mesh, RendResFailedComp) ? res->missingMesh
                                                                  : graphic->mesh;
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
  ecs_view_write_t(resourceItr, RendResComp)->state = RendResLoadState_Failed;
  ecs_world_add_empty_t(world, ecs_view_entity(resourceItr), RendResFailedComp);
}

static void rend_resource_load(
    RvkDevice* dev, const RendGlobalResComp* res, EcsWorld* world, EcsIterator* resourceItr) {

  const EcsEntityId       entity            = ecs_view_entity(resourceItr);
  RendResComp*            resComp           = ecs_view_write_t(resourceItr, RendResComp);
  const String            id                = asset_id(ecs_view_read_t(resourceItr, AssetComp));
  const AssetGraphicComp* maybeAssetGraphic = ecs_view_read_t(resourceItr, AssetGraphicComp);
  const AssetShaderComp*  maybeAssetShader  = ecs_view_read_t(resourceItr, AssetShaderComp);
  const AssetMeshComp*    maybeAssetMesh    = ecs_view_read_t(resourceItr, AssetMeshComp);
  const AssetTextureComp* maybeAssetTexture = ecs_view_read_t(resourceItr, AssetTextureComp);

  switch (resComp->state) {
  case RendResLoadState_AcquireAsset:
    asset_acquire(world, entity);
    break;
  case RendResLoadState_AcquireDependencies:
    if (ecs_world_has_t(world, entity, AssetFailedComp)) {
      rend_resource_fail(world, resourceItr);
      asset_release(world, entity);
      return;
    }
    if (!ecs_world_has_t(world, entity, AssetLoadedComp)) {
      return; // Wait for asset to be loaded.
    }
    if (maybeAssetGraphic) {
      bool dependenciesReady = true;

      // Shaders.
      array_ptr_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
        ecs_utils_maybe_add_t(world, ptr->shader, RendResComp);
        if (ecs_world_has_t(world, ptr->shader, RendResFailedComp)) {
          rend_resource_fail(world, resourceItr);
          asset_release(world, entity);
          return;
        }
        dependenciesReady &= ecs_world_has_t(world, ptr->shader, RendResLoadedComp);
      }

      // Mesh.
      if (maybeAssetGraphic->mesh) {
        ecs_utils_maybe_add_t(world, maybeAssetGraphic->mesh, RendResComp);
        const EcsEntityId meshEntity = rend_resource_mesh_entity(world, res, maybeAssetGraphic);
        dependenciesReady &= ecs_world_has_t(world, meshEntity, RendResLoadedComp);
      }

      // Textures.
      array_ptr_for_t(maybeAssetGraphic->samplers, AssetGraphicSampler, ptr) {
        ecs_utils_maybe_add_t(world, ptr->texture, RendResComp);
        const EcsEntityId textureEntity = rend_resource_texture_entity(world, res, ptr);
        dependenciesReady &= ecs_world_has_t(world, textureEntity, RendResLoadedComp);
      }

      if (!dependenciesReady) {
        return; // Wait for dependencies to be loaded.
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
        const EcsEntityId meshEntity = rend_resource_mesh_entity(world, res, maybeAssetGraphic);
        RendResMeshComp* meshComp = ecs_utils_write_t(world, MeshView, meshEntity, RendResMeshComp);
        rvk_graphic_mesh_add(graphicComp->graphic, meshComp->mesh);
      }

      // Add samplers.
      array_ptr_for_t(maybeAssetGraphic->samplers, AssetGraphicSampler, ptr) {
        const EcsEntityId   textureEntity = rend_resource_texture_entity(world, res, ptr);
        RendResTextureComp* comp =
            ecs_utils_write_t(world, TextureView, textureEntity, RendResTextureComp);
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
  case RendResLoadState_Loaded:
  case RendResLoadState_Failed:
    break;
  }
  ++resComp->state;
}

/**
 * Update all active resource loads.
 */
ecs_system_define(RendResLoadSys) {
  EcsIterator* globalItr = ecs_view_first(ecs_world_view_t(world, PlatformAndGlobalResView));
  if (!globalItr) {
    return;
  }

  const RendPlatformComp*  plat = ecs_view_read_t(globalItr, RendPlatformComp);
  const RendGlobalResComp* res  = ecs_view_read_t(globalItr, RendGlobalResComp);

  EcsView* resourceView = ecs_world_view_t(world, RendResLoadView);
  for (EcsIterator* itr = ecs_view_itr(resourceView); ecs_view_walk(itr);) {
    rend_resource_load(plat->device, res, world, itr);
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
  ecs_register_comp(RendResComp, .combinator = ecs_combine_resource);
  ecs_register_comp_empty(RendResLoadedComp);
  ecs_register_comp_empty(RendResFailedComp);
  ecs_register_comp(RendResUnloadComp);

  ecs_register_view(RendPlatView);
  ecs_register_view(PlatformAndGlobalResView);
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
      ecs_view_id(PlatformAndGlobalResView),
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
