#include "asset_graphic.h"
#include "asset_manager.h"
#include "asset_mesh.h"
#include "core_annotation.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_bounds.h"
#include "scene_renderable.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneBoundsComp);

typedef enum {
  SceneBoundsState_Start,
  SceneBoundsState_ProcessGraphic,
  SceneBoundsState_ProcessMesh,
} SceneBoundsState;

ecs_comp_define(SceneBoundsInitComp) {
  SceneBoundsState state;
  EcsEntityId      graphic, mesh;
  GeoBox           localBounds;
};

ecs_view_define(BoundsInitView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_maybe_write(SceneBoundsInitComp);
  ecs_access_without(SceneBoundsComp);
}
ecs_view_define(AssetGraphicView) { ecs_access_read(AssetGraphicComp); }
ecs_view_define(AssetMeshView) { ecs_access_read(AssetMeshComp); }

static void scene_bounds_init_done(EcsWorld* world, EcsIterator* itr) {
  const EcsEntityId          entity   = ecs_view_entity(itr);
  const SceneBoundsInitComp* initComp = ecs_view_read_t(itr, SceneBoundsInitComp);
  if (initComp->graphic) {
    asset_release(world, initComp->graphic);
  }
  if (initComp->mesh) {
    asset_release(world, initComp->mesh);
  }
  ecs_world_remove_t(world, entity, SceneBoundsInitComp);
  ecs_world_add_t(world, entity, SceneBoundsComp, .local = initComp->localBounds);
}

ecs_system_define(SceneBoundsInitSys) {
  EcsView* initView    = ecs_world_view_t(world, BoundsInitView);
  EcsView* graphicView = ecs_world_view_t(world, AssetGraphicView);
  EcsView* meshView    = ecs_world_view_t(world, AssetMeshView);

  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId    entity   = ecs_view_entity(itr);
    SceneBoundsInitComp* initComp = ecs_view_write_t(itr, SceneBoundsInitComp);
    if (!initComp) {
      ecs_world_add_t(world, entity, SceneBoundsInitComp, .localBounds = geo_box_inverted3());
      continue;
    }
    switch (initComp->state) {
    case SceneBoundsState_Start: {
      const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
      if (!renderable->graphic) {
        scene_bounds_init_done(world, itr);
        break;
      }
      initComp->graphic = renderable->graphic;
      asset_acquire(world, renderable->graphic);
      ++initComp->state;
      // Fallthrough.
    }
    case SceneBoundsState_ProcessGraphic: {
      const EcsIterator* graphicItr = ecs_view_maybe_at(graphicView, initComp->graphic);
      if (!graphicItr) {
        break; // Graphic has not loaded yet; wait.
      }
      const AssetGraphicComp* graphic = ecs_view_read_t(graphicItr, AssetGraphicComp);
      if (!graphic->mesh) {
        scene_bounds_init_done(world, itr);
        break;
      }
      initComp->mesh = graphic->mesh;
      asset_acquire(world, graphic->mesh);
      ++initComp->state;
      // Fallthrough.
    }
    case SceneBoundsState_ProcessMesh: {
      const EcsIterator* meshItr = ecs_view_maybe_at(meshView, initComp->mesh);
      if (!meshItr) {
        break; // Mesh has not loaded yet; wait.
      }
      const AssetMeshComp* mesh = ecs_view_read_t(meshItr, AssetMeshComp);
      initComp->localBounds     = mesh->positionBounds;
      scene_bounds_init_done(world, itr);
      break;
    }
    }
  }
}

ecs_module_init(scene_bounds_module) {
  ecs_register_comp(SceneBoundsComp);
  ecs_register_comp(SceneBoundsInitComp);

  ecs_register_view(BoundsInitView);
  ecs_register_view(AssetGraphicView);
  ecs_register_view(AssetMeshView);

  ecs_register_system(
      SceneBoundsInitSys,
      ecs_view_id(BoundsInitView),
      ecs_view_id(AssetGraphicView),
      ecs_view_id(AssetMeshView));
}
