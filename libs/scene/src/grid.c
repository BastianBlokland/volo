#include "asset_manager.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "scene_grid.h"
#include "scene_renderable.h"

static const u32 g_gridSegments          = 400;
static const u32 g_gridHighlightInterval = 5;
static const f32 g_gridCellSizeDefault   = 1.0f;
static const f32 g_gridCellSizeMin       = 0.1f;
static const f32 g_gridCellSizeMax       = 8.0f;

typedef struct {
  f32 cellSize;
  u32 segments;
  u32 highlightInterval;
} SceneGridData;

ecs_comp_define(SceneGridComp);

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(WindowView) { ecs_access_read(GapWindowComp); }

ecs_view_define(GridView) { ecs_access_write(SceneGridComp); }

ecs_system_define(SceneGridCreateSys) {
  if (ecs_utils_any(world, GridView)) {
    return;
  }

  EcsView*     view      = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr = ecs_view_maybe_at(view, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  const EcsEntityId gridEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, gridEntity, SceneGridComp, .cellSize = g_gridCellSizeDefault);
  ecs_world_add_t(
      world,
      gridEntity,
      SceneRenderableUniqueComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/grid.gra")));
}

ecs_system_define(SceneGridInputSys) {
  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, WindowView));
  EcsIterator* gridItr   = ecs_view_itr(ecs_world_view_t(world, GridView));

  while (ecs_view_walk(windowItr)) {
    const GapWindowComp* win = ecs_view_read_t(windowItr, GapWindowComp);

    ecs_view_itr_reset(gridItr);
    while (ecs_view_walk(gridItr)) {
      SceneGridComp* grid = ecs_view_write_t(gridItr, SceneGridComp);
      if (gap_window_key_pressed(win, GapKey_Plus)) {
        grid->cellSize *= 2.0f;
      }
      if (gap_window_key_pressed(win, GapKey_Minus)) {
        grid->cellSize *= 0.5f;
      }
      grid->cellSize = math_clamp_f32(grid->cellSize, g_gridCellSizeMin, g_gridCellSizeMax);
    }
  }
}

ecs_view_define(GridUpdateDataView) {
  ecs_access_read(SceneGridComp);
  ecs_access_write(SceneRenderableUniqueComp);
}

ecs_system_define(SceneGridUpdateDataSys) {
  EcsView* gridView = ecs_world_view_t(world, GridUpdateDataView);
  for (EcsIterator* itr = ecs_view_itr(gridView); ecs_view_walk(itr);) {
    const SceneGridComp*       grid       = ecs_view_read_t(itr, SceneGridComp);
    SceneRenderableUniqueComp* renderable = ecs_view_write_t(itr, SceneRenderableUniqueComp);

    renderable->vertexCountOverride = g_gridSegments * 4;

    SceneGridData* data = scene_renderable_unique_data(renderable, sizeof(SceneGridData)).ptr;
    *data               = (SceneGridData){
        .cellSize          = grid->cellSize,
        .segments          = g_gridSegments,
        .highlightInterval = g_gridHighlightInterval,
    };
  }
}

ecs_module_init(scene_grid_module) {
  ecs_register_comp(SceneGridComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(WindowView);
  ecs_register_view(GridView);
  ecs_register_view(GridUpdateDataView);

  ecs_register_system(SceneGridCreateSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GridView));
  ecs_register_system(SceneGridInputSys, ecs_view_id(WindowView), ecs_view_id(GridView));
  ecs_register_system(SceneGridUpdateDataSys, ecs_view_id(GridUpdateDataView));
}
