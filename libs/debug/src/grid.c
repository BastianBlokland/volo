#include "asset_manager.h"
#include "core_diag.h"
#include "core_math.h"
#include "debug_grid.h"
#include "debug_stats.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "input_manager.h"
#include "rend_draw.h"
#include "scene_lifetime.h"
#include "ui.h"

// clang-format off

static const String g_tooltipShow       = string_static("Should the grid be shown?");
static const String g_tooltipCellSize   = string_static("Size of the grid cells.");
static const String g_tooltipHeight     = string_static("Height to draw the grid at.");
static const String g_tooltipHighlight  = string_static("Every how manyth segment to be highlighted.");
static const String g_tooltipSegments   = string_static("How many segments the grid should consist of.");
static const String g_tooltipFade       = string_static("Fraction of the grid that should be faded out.");
static const f32    g_gridCellSizeMin   = 0.25f;
static const f32    g_gridCellSizeMax   = 4.0f;

// clang-format on

typedef struct {
  ALIGNAS(16)
  f32 cellSize;
  u32 segmentCount;
  u32 highlightInterval;
  f32 fadeFraction;
} DebugGridData;

ASSERT(sizeof(DebugGridData) == 16, "Size needs to match the size defined in glsl");
ASSERT(alignof(DebugGridData) == 16, "Alignment needs to match the glsl alignment");

ecs_comp_define(DebugGridComp) {
  EcsEntityId drawEntity;
  bool        show;
  f32         cellSize;
  f32         height;
  f32         highlightInterval;
  f32         segmentCount;
  f32         fadeFraction;
};

ecs_comp_define(DebugGridPanelComp) {
  UiPanel     panel;
  EcsEntityId window;
};

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(GridCreateView) {
  ecs_access_with(GapWindowComp);
  ecs_access_without(DebugGridComp);
}

ecs_view_define(GridReadView) { ecs_access_read(DebugGridComp); }
ecs_view_define(GridWriteView) { ecs_access_write(DebugGridComp); }

ecs_view_define(DrawWriteView) { ecs_access_write(RendDrawComp); }

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_write(DebugStatsGlobalComp);
}

ecs_view_define(UpdateView) {
  ecs_access_write(DebugGridPanelComp);
  ecs_access_write(UiCanvasComp);
}

static AssetManagerComp* debug_grid_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static void debug_grid_create(EcsWorld* world, const EcsEntityId entity, AssetManagerComp* assets) {
  const EcsEntityId drawEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, drawEntity, SceneLifetimeOwnerComp, .owner = entity);

  RendDrawComp* draw = rend_draw_create(world, drawEntity, RendDrawFlags_None);
  rend_draw_set_graphic(draw, asset_lookup(world, assets, string_lit("graphics/debug/grid.gra")));
  rend_draw_set_camera_filter(draw, entity);

  ecs_world_add_t(
      world,
      entity,
      DebugGridComp,
      .show              = true,
      .drawEntity        = drawEntity,
      .segmentCount      = 750,
      .cellSize          = 1.0f,
      .highlightInterval = 5,
      .fadeFraction      = 0.5);
}

ecs_system_define(DebugGridCreateSys) {
  AssetManagerComp* assets = debug_grid_asset_manager(world);
  if (!assets) {
    return;
  }
  EcsView* createView = ecs_world_view_t(world, GridCreateView);
  for (EcsIterator* itr = ecs_view_itr(createView); ecs_view_walk(itr);) {
    const EcsEntityId windowEntity = ecs_view_entity(itr);
    debug_grid_create(world, windowEntity, assets);
  }
}

ecs_system_define(DebugGridDrawSys) {
  EcsIterator* drawItr = ecs_view_itr(ecs_world_view_t(world, DrawWriteView));

  EcsView* gridView = ecs_world_view_t(world, GridReadView);
  for (EcsIterator* itr = ecs_view_itr(gridView); ecs_view_walk(itr);) {
    const DebugGridComp* grid = ecs_view_read_t(itr, DebugGridComp);
    if (!grid->show) {
      continue;
    }

    ecs_view_jump(drawItr, grid->drawEntity);
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

    rend_draw_set_vertex_count(draw, (u32)grid->segmentCount * 4);
    *rend_draw_add_instance_t(draw, DebugGridData, SceneTags_Debug, geo_box_inverted3()) =
        (DebugGridData){
            .cellSize          = grid->cellSize,
            .segmentCount      = (u32)grid->segmentCount,
            .highlightInterval = (u32)grid->highlightInterval,
            .fadeFraction      = grid->fadeFraction,
        };
  }
}

static void grid_notify_cell_size(DebugStatsGlobalComp* stats, const f32 cellSize) {
  debug_stats_notify(
      stats,
      string_lit("Grid size"),
      fmt_write_scratch("{}", fmt_float(cellSize, .maxDecDigits = 4, .expThresholdNeg = 0)));
}

static void grid_notify_height(DebugStatsGlobalComp* stats, const f32 height) {
  debug_stats_notify(
      stats,
      string_lit("Grid height"),
      fmt_write_scratch("{}", fmt_float(height, .maxDecDigits = 4, .expThresholdNeg = 0)));
}

static void grid_panel_draw(
    UiCanvasComp*         canvas,
    DebugStatsGlobalComp* stats,
    DebugGridPanelComp*   panelComp,
    DebugGridComp*        grid) {
  const String title = fmt_write_scratch("{} Grid Panel", fmt_ui_shape(Grid4x4));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Show"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &grid->show, .tooltip = g_tooltipShow)) {
    debug_stats_notify(
        stats, string_lit("Grid show"), grid->show ? string_lit("true") : string_lit("false"));
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Cell size"));
  ui_table_next_column(canvas, &table);
  if (ui_slider(
          canvas,
          &grid->cellSize,
          .min     = g_gridCellSizeMin,
          .max     = g_gridCellSizeMax,
          .step    = 0.25f,
          .tooltip = g_tooltipCellSize)) {
    grid_notify_cell_size(stats, grid->cellSize);
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Height"));
  ui_table_next_column(canvas, &table);
  f64 heightVal = grid->height;
  if (ui_numbox(canvas, &heightVal, .min = -250, .max = 250, .tooltip = g_tooltipHeight)) {
    grid->height = (f32)heightVal;
    grid_notify_height(stats, grid->height);
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Highlight"));
  ui_table_next_column(canvas, &table);
  ui_slider(
      canvas,
      &grid->highlightInterval,
      .min     = 2,
      .max     = 10,
      .step    = 1,
      .tooltip = g_tooltipHighlight);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Segments"));
  ui_table_next_column(canvas, &table);
  ui_slider(
      canvas,
      &grid->segmentCount,
      .min     = 50,
      .max     = 1000,
      .step    = 50,
      .tooltip = g_tooltipSegments);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Fade"));
  ui_table_next_column(canvas, &table);
  ui_slider(canvas, &grid->fadeFraction, .tooltip = g_tooltipFade);

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugGridUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DebugStatsGlobalComp*   stats = ecs_view_write_t(globalItr, DebugStatsGlobalComp);
  const InputManagerComp* input = ecs_view_read_t(globalItr, InputManagerComp);

  EcsIterator* gridItr = ecs_view_itr(ecs_world_view_t(world, GridWriteView));
  if (ecs_view_maybe_jump(gridItr, input_active_window(input))) {
    DebugGridComp* grid = ecs_view_write_t(gridItr, DebugGridComp);
    if (input_triggered_lit(input, "GridScaleUp")) {
      grid->cellSize = math_min(grid->cellSize * 2.0f, g_gridCellSizeMax);
      grid_notify_cell_size(stats, grid->cellSize);
    }
    if (input_triggered_lit(input, "GridScaleDown")) {
      grid->cellSize = math_max(grid->cellSize * 0.5f, g_gridCellSizeMin);
      grid_notify_cell_size(stats, grid->cellSize);
    }
  }

  EcsView* panelView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugGridPanelComp* panelComp = ecs_view_write_t(itr, DebugGridPanelComp);
    UiCanvasComp*       canvas    = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(gridItr, panelComp->window)) {
      // The window has been destroyed, this panel will be destroyed next frame.
      continue;
    }
    DebugGridComp* grid = ecs_view_write_t(gridItr, DebugGridComp);

    ui_canvas_reset(canvas);

    grid_panel_draw(canvas, stats, panelComp, grid);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_grid_module) {
  ecs_register_comp(DebugGridComp);
  ecs_register_comp(DebugGridPanelComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GridCreateView);
  ecs_register_view(GridReadView);
  ecs_register_view(GridWriteView);
  ecs_register_view(DrawWriteView);
  ecs_register_view(UpdateGlobalView);
  ecs_register_view(UpdateView);

  ecs_register_system(
      DebugGridCreateSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GridCreateView));

  ecs_register_system(DebugGridDrawSys, ecs_view_id(GridReadView), ecs_view_id(DrawWriteView));

  ecs_register_system(
      DebugGridUpdateSys,
      ecs_view_id(UpdateGlobalView),
      ecs_view_id(UpdateView),
      ecs_view_id(GridWriteView));
}

void debug_grid_snap(const DebugGridComp* comp, GeoVector* position) {
  for (u8 axis = 0; axis != 3; ++axis) {
    debug_grid_snap_axis(comp, position, axis);
  }
}

void debug_grid_snap_axis(const DebugGridComp* comp, GeoVector* position, const u8 axis) {
  diag_assert(axis < 3);
  const f32 round = math_round_nearest_f32(position->comps[axis] / comp->cellSize) * comp->cellSize;
  position->comps[axis] = round;
}

EcsEntityId debug_grid_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugGridPanelComp,
      .panel  = ui_panel(.position = ui_vector(0.75f, 0.5f), .size = ui_vector(330, 200)),
      .window = window);
  return panelEntity;
}
