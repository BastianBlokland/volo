#include "asset_graphic.h"
#include "asset_manager.h"
#include "asset_mesh.h"
#include "core_annotation.h"
#include "core_diag.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_bounds.h"
#include "scene_renderable.h"

#define scene_bounds_max_loads 16

ecs_comp_define_public(SceneBoundsComp);

typedef enum {
  BoundsTemplState_Start,
  BoundsTemplState_LoadGraphic,
  BoundsTemplState_LoadMesh,
  BoundsTemplState_Finished,
} BoundsTemplState;

/**
 * NOTE: On the graphic asset.
 */
ecs_comp_define(SceneBoundsTemplComp) {
  BoundsTemplState state;
  EcsEntityId      mesh;
  GeoBox           localBounds;
};

ecs_comp_define(SceneBoundsTemplLoadedComp);

static void ecs_combine_bounds_template(void* dataA, void* dataB) {
  SceneBoundsTemplComp* boundsA = dataA;
  SceneBoundsTemplComp* boundsB = dataB;

  (void)boundsA;
  (void)boundsB;
  diag_assert_msg(
      boundsA->state == BoundsTemplState_Start && boundsB->state == BoundsTemplState_Start,
      "Bounds templates can only be combined in the starting phase");
}

ecs_view_define(BoundsInitView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_without(SceneBoundsComp);
}

ecs_view_define(BoundsTemplView) { ecs_access_read(SceneBoundsTemplComp); }

ecs_system_define(SceneBoundsInitSys) {
  EcsView*     initView    = ecs_world_view_t(world, BoundsInitView);
  EcsIterator* templateItr = ecs_view_itr(ecs_world_view_t(world, BoundsTemplView));

  u32 startedLoads = 0;

  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    const EcsEntityId          graphic    = renderable->graphic;
    if (!graphic) {
      ecs_world_add_t(world, entity, SceneBoundsComp, .local = geo_box_inverted3());
      continue;
    }

    if (ecs_view_maybe_jump(templateItr, graphic)) {
      const SceneBoundsTemplComp* templ = ecs_view_read_t(templateItr, SceneBoundsTemplComp);
      if (templ->state == BoundsTemplState_Finished) {
        ecs_world_add_t(world, entity, SceneBoundsComp, .local = templ->localBounds);
      }
      continue;
    }

    if (++startedLoads > scene_bounds_max_loads) {
      continue; // Limit the amount of loads to start in a single frame.
    }
    ecs_world_add_t(world, graphic, SceneBoundsTemplComp, .localBounds = geo_box_inverted3());
  }
}

ecs_view_define(TemplateLoadView) {
  ecs_access_write(SceneBoundsTemplComp);
  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_without(SceneBoundsTemplLoadedComp);
}
ecs_view_define(MeshView) { ecs_access_read(AssetMeshComp); }

static bool scene_asset_is_loaded(EcsWorld* world, const EcsEntityId asset) {
  return ecs_world_has_t(world, asset, AssetLoadedComp) ||
         ecs_world_has_t(world, asset, AssetFailedComp);
}

static void scene_bounds_template_load_done(EcsWorld* world, EcsIterator* itr) {
  const EcsEntityId     entity     = ecs_view_entity(itr);
  SceneBoundsTemplComp* boundsComp = ecs_view_write_t(itr, SceneBoundsTemplComp);

  asset_release(world, entity);
  if (boundsComp->mesh) {
    asset_release(world, boundsComp->mesh);
  }
  boundsComp->state = BoundsTemplState_Finished;
  ecs_world_add_empty_t(world, entity, SceneBoundsTemplLoadedComp);
}

ecs_system_define(SceneBoundsTemplateLoadSys) {
  EcsView*     loadView = ecs_world_view_t(world, TemplateLoadView);
  EcsIterator* meshItr  = ecs_view_itr(ecs_world_view_t(world, MeshView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId       entity       = ecs_view_entity(itr);
    SceneBoundsTemplComp*   templateComp = ecs_view_write_t(itr, SceneBoundsTemplComp);
    const AssetGraphicComp* graphic      = ecs_view_read_t(itr, AssetGraphicComp);
    switch (templateComp->state) {
    case BoundsTemplState_Start: {
      asset_acquire(world, entity);
      ++templateComp->state;
      // Fallthrough.
    }
    case BoundsTemplState_LoadGraphic: {
      if (!scene_asset_is_loaded(world, entity)) {
        break; // Graphic has not loaded yet; wait.
      }
      if (!graphic) {
        scene_bounds_template_load_done(world, itr);
        break; // Graphic failed to load, or was of unexpected type.
      }
      if (!graphic->mesh) {
        scene_bounds_template_load_done(world, itr);
        break; // Graphic did not have a mesh.
      }
      templateComp->mesh = graphic->mesh;
      asset_acquire(world, graphic->mesh);
      ++templateComp->state;
      // Fallthrough.
    }
    case BoundsTemplState_LoadMesh: {
      if (!scene_asset_is_loaded(world, templateComp->mesh)) {
        break; // Mesh has not loaded yet; wait.
      }
      if (ecs_view_maybe_jump(meshItr, templateComp->mesh)) {
        const AssetMeshComp* mesh = ecs_view_read_t(meshItr, AssetMeshComp);
        templateComp->localBounds = mesh->positionBounds;
      }
      scene_bounds_template_load_done(world, itr);
      break;
    }
    case BoundsTemplState_Finished:
      diag_crash();
    }
  }
}

ecs_view_define(DirtyGraphicsView) {
  ecs_access_with(SceneBoundsTemplComp);
  ecs_access_with(SceneBoundsTemplLoadedComp);
  ecs_access_with(AssetChangedComp);
}

ecs_view_define(RenderablesWithBoundsView) {
  ecs_access_with(SceneBoundsComp);
  ecs_access_read(SceneRenderableComp);
}

ecs_system_define(SceneClearDirtyBoundsSys) {
  /**
   * Clear bounds templates on changed graphic assets.
   */
  EcsView* dirtyGraphicsView = ecs_world_view_t(world, DirtyGraphicsView);
  for (EcsIterator* itr = ecs_view_itr(dirtyGraphicsView); ecs_view_walk(itr);) {
    ecs_world_remove_t(world, ecs_view_entity(itr), SceneBoundsTemplComp);
    ecs_world_remove_t(world, ecs_view_entity(itr), SceneBoundsTemplLoadedComp);
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
  ecs_register_comp(SceneBoundsTemplComp, .combinator = ecs_combine_bounds_template);
  ecs_register_comp_empty(SceneBoundsTemplLoadedComp);

  ecs_register_view(BoundsInitView);
  ecs_register_view(BoundsTemplView);

  ecs_register_view(TemplateLoadView);
  ecs_register_view(MeshView);

  ecs_register_view(DirtyGraphicsView);
  ecs_register_view(RenderablesWithBoundsView);

  ecs_register_system(
      SceneBoundsInitSys, ecs_view_id(BoundsInitView), ecs_view_id(BoundsTemplView));

  ecs_register_system(
      SceneBoundsTemplateLoadSys, ecs_view_id(TemplateLoadView), ecs_view_id(MeshView));

  ecs_register_system(
      SceneClearDirtyBoundsSys,
      ecs_view_id(DirtyGraphicsView),
      ecs_view_id(RenderablesWithBoundsView));
}
