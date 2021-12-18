#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_graphic.h"

#include "platform_internal.h"
#include "resource_internal.h"
#include "rvk/device_internal.h"

ecs_comp_define_public(RendGraphicComp);
ecs_comp_define_public(RendShaderComp);
ecs_comp_define_public(RendMeshComp);

static void ecs_destruct_graphic_comp(void* data) {
  RendGraphicComp* comp = data;
  if (comp->graphic) {
    rvk_graphic_destroy(comp->graphic);
  }
}

static void ecs_destruct_shader_comp(void* data) {
  RendShaderComp* comp = data;
  if (comp->shader) {
    rvk_shader_destroy(comp->shader);
  }
}

static void ecs_destruct_mesh_comp(void* data) {
  RendMeshComp* comp = data;
  if (comp->mesh) {
    rvk_mesh_destroy(comp->mesh);
  }
}

typedef enum {
  RendResourceState_AcquireAsset,
  RendResourceState_AcquireDependencies,
  RendResourceState_Create,
  RendResourceState_Ready,
} RendResourceState;

ecs_comp_define(RendResource) { RendResourceState state; };
ecs_comp_define(RendResourceReady);

static void ecs_combine_resource(void* dataA, void* dataB) {
  RendResource* compA = dataA;
  RendResource* compB = dataB;
  compA->state        = math_max(compA->state, compB->state);
}

ecs_view_define(RendPlatformView) { ecs_access_read(RendPlatformComp); }
ecs_view_define(SceneGraphicView) { ecs_access_read(SceneGraphicComp); };

ecs_view_define(ShaderView) { ecs_access_write(RendShaderComp); };
ecs_view_define(GraphicView) { ecs_access_write(RendGraphicComp); };
ecs_view_define(MeshView) { ecs_access_write(RendMeshComp); };

ecs_view_define(RendResourceLoadView) {
  ecs_access_without(RendResourceReady);
  ecs_access_write(RendResource);

  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_maybe_read(AssetShaderComp);
  ecs_access_maybe_read(AssetMeshComp);
};

ecs_system_define(RendResourceRequestSys) {
  EcsView* graphicView = ecs_world_view_t(world, SceneGraphicView);
  for (EcsIterator* itr = ecs_view_itr(graphicView); ecs_view_walk(itr);) {
    const SceneGraphicComp* comp = ecs_view_read_t(itr, SceneGraphicComp);
    ecs_utils_maybe_add_t(world, comp->asset, RendResource);
  }
}

static void rend_resource_load(RvkDevice* rvkDev, EcsWorld* world, EcsIterator* resourceItr) {
  const EcsEntityId       entity            = ecs_view_entity(resourceItr);
  RendResource*           resourceComp      = ecs_view_write_t(resourceItr, RendResource);
  const AssetGraphicComp* maybeAssetGraphic = ecs_view_read_t(resourceItr, AssetGraphicComp);
  const AssetShaderComp*  maybeAssetShader  = ecs_view_read_t(resourceItr, AssetShaderComp);
  const AssetMeshComp*    maybeAssetMesh    = ecs_view_read_t(resourceItr, AssetMeshComp);

  switch (resourceComp->state) {
  case RendResourceState_AcquireAsset:
    asset_acquire(world, entity);
    break;
  case RendResourceState_AcquireDependencies:
    if (!ecs_world_has_t(world, entity, AssetLoadedComp)) {
      return; // Wait for asset to be loaded.
    }
    if (maybeAssetGraphic) {
      bool dependenciesReady = true;

      // Shaders.
      array_ptr_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
        ecs_utils_maybe_add_t(world, ptr->shader, RendResource);
        dependenciesReady &= ecs_world_has_t(world, ptr->shader, RendResourceReady);
      }

      // Mesh.
      ecs_utils_maybe_add_t(world, maybeAssetGraphic->mesh, RendResource);
      dependenciesReady &= ecs_world_has_t(world, maybeAssetGraphic->mesh, RendResourceReady);

      if (!dependenciesReady) {
        return; // Wait for dependencies to be loaded.
      }
    }
    break;
  case RendResourceState_Create: {
    if (maybeAssetGraphic) {
      RendGraphicComp* graphicComp = ecs_world_add_t(
          world, entity, RendGraphicComp, .graphic = rvk_graphic_create(rvkDev, maybeAssetGraphic));

      // Add shaders.
      array_ptr_for_t(maybeAssetGraphic->shaders, AssetGraphicShader, ptr) {
        RendShaderComp* comp = ecs_utils_write_t(world, ShaderView, ptr->shader, RendShaderComp);
        rvk_graphic_shader_add(graphicComp->graphic, comp->shader);
      }

      // Add mesh.
      RendMeshComp* meshComp =
          ecs_utils_write_t(world, MeshView, maybeAssetGraphic->mesh, RendMeshComp);
      rvk_graphic_mesh_add(graphicComp->graphic, meshComp->mesh);

    } else if (maybeAssetShader) {
      ecs_world_add_t(
          world, entity, RendShaderComp, .shader = rvk_shader_create(rvkDev, maybeAssetShader));
    } else if (maybeAssetMesh) {
      ecs_world_add_t(world, entity, RendMeshComp, .mesh = rvk_mesh_create(rvkDev, maybeAssetMesh));
    } else {
      diag_crash_msg("Unsupported resource asset type");
    }
    asset_release(world, entity);
    ecs_world_add_empty_t(world, entity, RendResourceReady);
  }
  case RendResourceState_Ready:
    break;
  }
  ++resourceComp->state;
}

ecs_system_define(RendResourceLoadSys) {
  const RendPlatformComp* plat = ecs_utils_read_first_t(world, RendPlatformView, RendPlatformComp);
  if (!plat) {
    return;
  }
  RvkDevice* rvkDev = rvk_platform_device(plat->vulkan);

  EcsView* resourceView = ecs_world_view_t(world, RendResourceLoadView);
  for (EcsIterator* itr = ecs_view_itr(resourceView); ecs_view_walk(itr);) {
    rend_resource_load(rvkDev, world, itr);
  }
}

ecs_module_init(rend_resource_module) {
  ecs_register_comp(RendGraphicComp, .destructor = ecs_destruct_graphic_comp);
  ecs_register_comp(RendShaderComp, .destructor = ecs_destruct_shader_comp);
  ecs_register_comp(RendMeshComp, .destructor = ecs_destruct_mesh_comp);

  ecs_register_comp(RendResource, .combinator = ecs_combine_resource);
  ecs_register_comp_empty(RendResourceReady);

  ecs_register_view(RendPlatformView);
  ecs_register_view(SceneGraphicView);
  ecs_register_view(ShaderView);
  ecs_register_view(GraphicView);
  ecs_register_view(MeshView);

  ecs_register_view(RendResourceLoadView);

  ecs_register_system(RendResourceRequestSys, ecs_view_id(SceneGraphicView));
  ecs_register_system(
      RendResourceLoadSys,
      ecs_view_id(RendPlatformView),
      ecs_view_id(RendResourceLoadView),
      ecs_view_id(ShaderView),
      ecs_view_id(MeshView));
}

void rend_resource_teardown(EcsWorld* world) {
  const RendPlatformComp* plat = ecs_utils_read_first_t(world, RendPlatformView, RendPlatformComp);
  if (plat) {
    // Wait for all rendering to be done.
    rvk_platform_wait_idle(plat->vulkan);
  }

  // Teardown graphics.
  EcsView* graphicView = ecs_world_view_t(world, GraphicView);
  for (EcsIterator* itr = ecs_view_itr(graphicView); ecs_view_walk(itr);) {
    RendGraphicComp* comp = ecs_view_write_t(itr, RendGraphicComp);
    rvk_graphic_destroy(comp->graphic);
    comp->graphic = null;
  }

  // Teardown shaders.
  EcsView* shaderView = ecs_world_view_t(world, ShaderView);
  for (EcsIterator* itr = ecs_view_itr(shaderView); ecs_view_walk(itr);) {
    RendShaderComp* comp = ecs_view_write_t(itr, RendShaderComp);
    rvk_shader_destroy(comp->shader);
    comp->shader = null;
  }

  // Teardown meshes.
  EcsView* meshView = ecs_world_view_t(world, MeshView);
  for (EcsIterator* itr = ecs_view_itr(meshView); ecs_view_walk(itr);) {
    RendMeshComp* comp = ecs_view_write_t(itr, RendMeshComp);
    rvk_mesh_destroy(comp->mesh);
    comp->mesh = null;
  }
}
