#include "asset_manager.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "rend_draw.h"

static const u32 g_gridSegments          = 400;
static const u32 g_gridHighlightInterval = 5;
static const f32 g_gridCellSizeDefault   = 1.0f;
static const f32 g_gridCellSizeMin       = 0.1f;
static const f32 g_gridCellSizeMax       = 8.0f;

typedef struct {
  f32 cellSize;
  u32 segments;
  u32 highlightInterval;
} DebugGridData;

ecs_comp_define(DebugGridComp) { f32 cellSize; };

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(WindowView) { ecs_access_read(GapWindowComp); }

ecs_view_define(GridView) { ecs_access_write(DebugGridComp); }

ecs_system_define(DebugGridCreateSys) {
  if (ecs_utils_any(world, GridView)) {
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  const EcsEntityId gridEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, gridEntity, DebugGridComp, .cellSize = g_gridCellSizeDefault);

  RendDrawComp* draw = rend_draw_create(world, gridEntity, RendDrawFlags_NoInstanceFiltering);
  rend_draw_set_graphic(draw, asset_lookup(world, assets, string_lit("graphics/debug/grid.gra")));
  rend_draw_set_vertex_count(draw, g_gridSegments * 4);
}

ecs_system_define(DebugGridInputSys) {
  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, WindowView));
  EcsIterator* gridItr   = ecs_view_itr(ecs_world_view_t(world, GridView));

  while (ecs_view_walk(windowItr)) {
    const GapWindowComp* win = ecs_view_read_t(windowItr, GapWindowComp);

    ecs_view_itr_reset(gridItr);
    while (ecs_view_walk(gridItr)) {
      DebugGridComp* grid = ecs_view_write_t(gridItr, DebugGridComp);
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
  ecs_access_read(DebugGridComp);
  ecs_access_write(RendDrawComp);
}

ecs_system_define(DebugGridUpdateDataSys) {
  EcsView* gridView = ecs_world_view_t(world, GridUpdateDataView);
  for (EcsIterator* itr = ecs_view_itr(gridView); ecs_view_walk(itr);) {
    const DebugGridComp* grid = ecs_view_read_t(itr, DebugGridComp);
    RendDrawComp*        draw = ecs_view_write_t(itr, RendDrawComp);

    const DebugGridData data = {
        .cellSize          = grid->cellSize,
        .segments          = g_gridSegments,
        .highlightInterval = g_gridHighlightInterval,
    };
    rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, (GeoBox){0});
  }
}

ecs_module_init(debug_grid_module) {
  ecs_register_comp(DebugGridComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(WindowView);
  ecs_register_view(GridView);
  ecs_register_view(GridUpdateDataView);

  ecs_register_system(DebugGridCreateSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GridView));
  ecs_register_system(DebugGridInputSys, ecs_view_id(WindowView), ecs_view_id(GridView));
  ecs_register_system(DebugGridUpdateDataSys, ecs_view_id(GridUpdateDataView));
}
