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

#define scene_bounds_max_loads 16

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
  SceneGraphicBoundsComp* boundsA = dataA;
  SceneGraphicBoundsComp* boundsB = dataB;

  MAYBE_UNUSED const bool identical =
      geo_vector_equal(boundsA->localBounds.min, boundsB->localBounds.min, 1e-3f) &&
      geo_vector_equal(boundsA->localBounds.max, boundsB->localBounds.max, 1e-3f);

  diag_assert_msg(identical, "Only identical graphic-bounds can be combined");
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

  u32 startedLoads = 0;

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
      if (++startedLoads > scene_bounds_max_loads) {
        continue; // Limit the amount of loads to start in a single frame.
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

ecs_view_define(DirtyGraphicsView) {
  ecs_access_with(SceneGraphicBoundsComp);
  ecs_access_with(AssetChangedComp);
}

ecs_view_define(RenderablesWithBoundsView) {
  ecs_access_with(SceneBoundsComp);
  ecs_access_read(SceneRenderableComp);
}

ecs_system_define(SceneClearDirtyBoundsSys) {
  /**
   * Clear cached bounds on changed graphic assets.
   */
  EcsView* dirtyGraphicsView = ecs_world_view_t(world, DirtyGraphicsView);
  for (EcsIterator* itr = ecs_view_itr(dirtyGraphicsView); ecs_view_walk(itr);) {
    ecs_world_remove_t(world, ecs_view_entity(itr), SceneGraphicBoundsComp);
  }

  /**
   * Clear computed bounds on renderable entities when their graphic asset has changed.
   */
  EcsView* renderablesView = ecs_world_view_t(world, RenderablesWithBoundsView);
  for (EcsIterator* itr = ecs_view_itr(renderablesView); ecs_view_walk(itr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    if (ecs_world_has_t(world, renderable->graphic, AssetChangedComp)) {
      ecs_world_remove_t(world, ecs_view_entity(itr), SceneBoundsComp);
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
  ecs_register_view(DirtyGraphicsView);
  ecs_register_view(RenderablesWithBoundsView);

  ecs_register_system(
      SceneBoundsInitSys,
      ecs_view_id(BoundsInitView),
      ecs_view_id(GraphicView),
      ecs_view_id(GraphicBoundsView),
      ecs_view_id(MeshView));

  ecs_register_system(
      SceneClearDirtyBoundsSys,
      ecs_view_id(DirtyGraphicsView),
      ecs_view_id(RenderablesWithBoundsView));
}
