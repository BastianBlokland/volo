#include "asset_manager.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_graphic.h"

#include "platform_internal.h"
#include "resource_internal.h"
#include "rvk/platform_internal.h"

ecs_comp_define_public(RendGraphicComp);
ecs_comp_define_public(RendShaderComp);

static void ecs_destruct_graphic_comp(void* data) {
  RendGraphicComp* comp = data;
  rvk_graphic_destroy(comp->graphic);
}

static void ecs_destruct_shader_comp(void* data) {
  RendShaderComp* comp = data;
  rvk_shader_destroy(comp->shader);
}

typedef enum {
  RendResourceState_AssetAcquire,
  RendResourceState_DependencyAcquire,
  RendResourceState_DependencyWait,
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

static void rend_resource_request(EcsWorld* world, const EcsEntityId entity) {
  if (!ecs_world_has_t(world, entity, RendResource)) {
    ecs_world_add_t(world, entity, RendResource);
  }
}

ecs_view_define(RendPlatformView) { ecs_access_read(RendPlatformComp); }
ecs_view_define(SceneGraphicView) { ecs_access_read(SceneGraphicComp); };

ecs_view_define(RendResourceLoadView) {
  ecs_access_without(RendResourceReady);
  ecs_access_write(RendResource);

  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_maybe_read(AssetShaderComp);
};

ecs_system_define(RendResourceRequestSys) {
  EcsView* graphicView = ecs_world_view_t(world, SceneGraphicView);
  for (EcsIterator* itr = ecs_view_itr(graphicView); ecs_view_walk(itr);) {
    const SceneGraphicComp* graphicComp = ecs_view_read_t(itr, SceneGraphicComp);
    rend_resource_request(world, graphicComp->asset);
  }
}

ecs_system_define(RendResourceLoadSys) {
  const RendPlatformComp* plat = ecs_utils_read_first_t(world, RendPlatformView, RendPlatformComp);
  if (!plat) {
    return;
  }

  EcsView* resourceView = ecs_world_view_t(world, RendResourceLoadView);
  for (EcsIterator* itr = ecs_view_itr(resourceView); ecs_view_walk(itr);) {
    const EcsEntityId       entity            = ecs_view_entity(itr);
    RendResource*           resourceComp      = ecs_view_write_t(itr, RendResource);
    const AssetGraphicComp* maybeAssetGraphic = ecs_view_read_t(itr, AssetGraphicComp);
    const AssetShaderComp*  maybeAssetShader  = ecs_view_read_t(itr, AssetShaderComp);

    switch (resourceComp->state) {
    case RendResourceState_AssetAcquire:
      asset_acquire(world, entity);
      break;
    case RendResourceState_DependencyAcquire:
      if (!ecs_world_has_t(world, entity, AssetLoadedComp)) {
        continue; // Wait for asset to be loaded.
      }
      if (maybeAssetGraphic) {
        for (usize i = 0; i != maybeAssetGraphic->shaders.count; ++i) {
          rend_resource_request(world, maybeAssetGraphic->shaders.values[i].shader);
        }
      }
      break;
    case RendResourceState_DependencyWait:
      if (maybeAssetGraphic) {
        for (usize i = 0; i != maybeAssetGraphic->shaders.count; ++i) {
          if (!ecs_world_has_t(
                  world, maybeAssetGraphic->shaders.values[i].shader, RendResourceReady)) {
            continue; // Wait for dependency to be ready.
          }
        }
      } else if (maybeAssetShader) {
        // No dependencies.
      } else {
        diag_crash_msg("Unsupported resource asset type");
      }
      break;
    case RendResourceState_Create: {
      if (maybeAssetGraphic) {
        // TODO:
      } else if (maybeAssetShader) {
        ecs_world_add_t(
            world,
            entity,
            RendShaderComp,
            .shader = rvk_shader_create(rvk_platform_device(plat->vulkan), maybeAssetShader));
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
}

ecs_module_init(rend_resource_module) {
  ecs_register_comp(RendGraphicComp, .destructor = ecs_destruct_graphic_comp);
  ecs_register_comp(RendShaderComp, .destructor = ecs_destruct_shader_comp);

  ecs_register_comp(RendResource, .combinator = ecs_combine_resource);
  ecs_register_comp_empty(RendResourceReady);

  ecs_register_view(RendPlatformView);
  ecs_register_view(SceneGraphicView);
  ecs_register_view(RendResourceLoadView);

  ecs_register_system(RendResourceRequestSys, ecs_view_id(SceneGraphicView));
  ecs_register_system(
      RendResourceLoadSys, ecs_view_id(RendPlatformView), ecs_view_id(RendResourceLoadView));
}
