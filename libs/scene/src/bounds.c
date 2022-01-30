#include "asset_graphic.h"
#include "asset_manager.h"
#include "asset_mesh.h"
#include "core_annotation.h"
#include "core_diag.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_bounds.h"
#include "scene_renderable.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneBoundsComp);

/**
 * When computing a graphic's bounds we store it on the graphic entity to avoid having to re-compute
 * it (and thus load the graphic + mesh) later.
 */
ecs_comp_define(SceneGraphicBoundsComp) { GeoBox localBounds; };

typedef enum {
  SceneBoundsState_Start,
  SceneBoundsState_LoadGraphic,
  SceneBoundsState_LoadMesh,
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
ecs_view_define(GraphicView) { ecs_access_read(AssetGraphicComp); }
ecs_view_define(GraphicBoundsView) { ecs_access_read(SceneGraphicBoundsComp); }
ecs_view_define(MeshView) { ecs_access_read(AssetMeshComp); }

static void ecs_combine_graphic_bounds(void* dataA, void* dataB) {
  MAYBE_UNUSED SceneGraphicBoundsComp* boundsA = dataA;
  MAYBE_UNUSED SceneGraphicBoundsComp* boundsB = dataB;
  diag_assert_msg(
      geo_vector_equal(boundsA->localBounds.min, boundsB->localBounds.min, 1e-3f),
      "Only identical graphic-bounds can be combined");
  diag_assert_msg(
      geo_vector_equal(boundsA->localBounds.max, boundsB->localBounds.max, 1e-3f),
      "Only identical graphic-bounds can be combined");
}

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
  EcsView*     initView         = ecs_world_view_t(world, BoundsInitView);
  EcsIterator* graphicItr       = ecs_view_itr(ecs_world_view_t(world, GraphicView));
  EcsIterator* graphicBoundsItr = ecs_view_itr(ecs_world_view_t(world, GraphicBoundsView));
  EcsIterator* meshItr          = ecs_view_itr(ecs_world_view_t(world, MeshView));

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
      if (ecs_view_maybe_jump(graphicBoundsItr, renderable->graphic)) {
        // Graphic already has bounds computed; use them to avoid reloading the graphic + mesh.
        initComp->localBounds =
            ecs_view_read_t(graphicBoundsItr, SceneGraphicBoundsComp)->localBounds;
        scene_bounds_init_done(world, itr);
        break;
      }
      initComp->graphic = renderable->graphic;
      asset_acquire(world, renderable->graphic);
      ++initComp->state;
      // Fallthrough.
    }
    case SceneBoundsState_LoadGraphic: {
      if (!ecs_view_maybe_jump(graphicItr, initComp->graphic)) {
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
    case SceneBoundsState_LoadMesh: {
      if (!ecs_view_maybe_jump(meshItr, initComp->mesh)) {
        break; // Mesh has not loaded yet; wait.
      }
      const AssetMeshComp* mesh = ecs_view_read_t(meshItr, AssetMeshComp);
      initComp->localBounds     = mesh->positionBounds;
      scene_bounds_init_done(world, itr);
      ecs_world_add_t(
          world, initComp->graphic, SceneGraphicBoundsComp, .localBounds = mesh->positionBounds);
      break;
    }
    }
  }
}

ecs_module_init(scene_bounds_module) {
  ecs_register_comp(SceneBoundsComp);
  ecs_register_comp(SceneGraphicBoundsComp, .combinator = ecs_combine_graphic_bounds);
  ecs_register_comp(SceneBoundsInitComp);

  ecs_register_view(BoundsInitView);
  ecs_register_view(GraphicView);
  ecs_register_view(GraphicBoundsView);
  ecs_register_view(MeshView);

  ecs_register_system(
      SceneBoundsInitSys,
      ecs_view_id(BoundsInitView),
      ecs_view_id(GraphicView),
      ecs_view_id(GraphicBoundsView),
      ecs_view_id(MeshView));
}
