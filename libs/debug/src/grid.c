#include "asset_manager.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "rend_draw.h"
#include "scene_lifetime.h"
#include "ui.h"

typedef struct {
  ALIGNAS(16)
  f32 cellSize;
  u32 segmentCount;
  u32 highlightInterval;
  f32 fadeFraction;
} DebugGridData;

ASSERT(sizeof(DebugGridData) == 16, "Size needs to match the size defined in glsl");

ecs_comp_define(DebugGridComp) {
  EcsEntityId drawEntity;
  bool        show;
  f32         cellSize;
  f32         highlightInterval;
  f32         segmentCount;
  f32         fadeFraction;
};

ecs_comp_define(DebugGridPanelComp) {
  UiPanelState state;
  EcsEntityId  window;
};

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(GridCreateView) {
  ecs_access_with(GapWindowComp);
  ecs_access_without(DebugGridComp);
}

ecs_view_define(GridReadView) { ecs_access_read(DebugGridComp); }
ecs_view_define(GridWriteView) { ecs_access_write(DebugGridComp); }

ecs_view_define(DrawWriteView) { ecs_access_write(RendDrawComp); }

ecs_view_define(PanelUpdateView) {
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

  RendDrawComp* draw = rend_draw_create(world, drawEntity, RendDrawFlags_NoInstanceFiltering);
  rend_draw_set_graphic(draw, asset_lookup(world, assets, string_lit("graphics/debug/grid.gra")));
  rend_draw_set_camera_filter(draw, entity);

  ecs_world_add_t(
      world,
      entity,
      DebugGridComp,
      .show              = true,
      .drawEntity        = drawEntity,
      .segmentCount      = 400,
      .cellSize          = 1,
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

    const DebugGridData data = {
        .cellSize          = grid->cellSize,
        .segmentCount      = (u32)grid->segmentCount,
        .highlightInterval = (u32)grid->highlightInterval,
        .fadeFraction      = grid->fadeFraction,
    };
    rend_draw_set_vertex_count(draw, (u32)grid->segmentCount * 4);
    rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, (GeoBox){0});
  }
}

static void grid_panel_draw(UiCanvasComp* canvas, DebugGridPanelComp* panel, DebugGridComp* grid) {
  const String title = fmt_write_scratch("{} Grid Settings", fmt_ui_shape(Grid4x4));
  ui_panel_begin(canvas, &panel->state, .title = title);

  UiGridState layoutGrid = ui_grid_init(canvas, .size = {100, 20});

  ui_label(canvas, string_lit("Show"));
  ui_grid_next_col(canvas, &layoutGrid);
  ui_toggle(canvas, &grid->show, .tooltip = string_lit("Should the grid be shown?"));
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Cell size"));
  ui_grid_next_col(canvas, &layoutGrid);
  ui_slider(
      canvas,
      &grid->cellSize,
      .min     = 0.1f,
      .max     = 4,
      .step    = 0.1f,
      .tooltip = string_lit("Size of the grid cells"));
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Highlight"));
  ui_grid_next_col(canvas, &layoutGrid);
  ui_slider(
      canvas,
      &grid->highlightInterval,
      .min     = 2,
      .max     = 10,
      .step    = 1,
      .tooltip = string_lit("Every how manyth segment to be highlighted"));
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Segments"));
  ui_grid_next_col(canvas, &layoutGrid);
  ui_slider(
      canvas,
      &grid->segmentCount,
      .min     = 50,
      .max     = 1000,
      .step    = 50,
      .tooltip = string_lit("How many segments the grid should consist of"));
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Fade"));
  ui_grid_next_col(canvas, &layoutGrid);
  ui_slider(
      canvas,
      &grid->fadeFraction,
      .tooltip = string_lit("Fraction of the grid that should be faded out"));

  ui_panel_end(canvas, &panel->state);
}

ecs_system_define(DebugGridUpdatePanelSys) {
  EcsIterator* gridItr = ecs_view_itr(ecs_world_view_t(world, GridWriteView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugGridPanelComp* panel  = ecs_view_write_t(itr, DebugGridPanelComp);
    UiCanvasComp*       canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(gridItr, panel->window)) {
      // The window has been destroyed, this panel will be destroyed next frame.
      continue;
    }
    DebugGridComp* grid = ecs_view_write_t(gridItr, DebugGridComp);

    ui_canvas_reset(canvas);
    grid_panel_draw(canvas, panel, grid);

    if (panel->state.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
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
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugGridCreateSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GridCreateView));

  ecs_register_system(DebugGridDrawSys, ecs_view_id(GridReadView), ecs_view_id(DrawWriteView));

  ecs_register_system(
      DebugGridUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(GridWriteView));
}

EcsEntityId debug_grid_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugGridPanelComp,
      .state  = ui_panel_init(ui_vector(225, 185)),
      .window = window);
  return panelEntity;
}
