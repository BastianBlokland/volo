#include "ecs_world.h"
#include "rend_settings.h"
#include "ui.h"

ecs_comp_define(DebugRendPanelComp) {
  UiPanelState state;
  EcsEntityId  window;
};

ecs_view_define(RendSettingsView) { ecs_access_write(RendSettingsComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugRendPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void
rend_panel_draw(UiCanvasComp* canvas, DebugRendPanelComp* panel, RendSettingsComp* settings) {
  const String title = fmt_write_scratch("{} Renderer Settings", fmt_ui_shape(Brush));
  ui_panel_begin(canvas, &panel->state, .title = title);

  UiGridState layoutGrid = ui_grid_init(canvas, .size = {110, 25});

  ui_label(canvas, string_lit("VSync"));
  ui_grid_next_col(canvas, &layoutGrid);
  bool vsync = settings->presentMode == RendPresentMode_VSyncRelaxed;
  if (ui_toggle(canvas, &vsync, .tooltip = string_lit("Should presentation wait for VBlanks?"))) {
    settings->presentMode = vsync ? RendPresentMode_VSyncRelaxed : RendPresentMode_Immediate;
  }
  ui_grid_next_row(canvas, &layoutGrid);

  if (ui_button(canvas, .label = string_lit("Reset"))) {
    rend_settings_to_default(settings);
  }

  ui_panel_end(canvas, &panel->state);
}

ecs_system_define(DebugRendUpdatePanelSys) {
  EcsIterator* settingsItr = ecs_view_itr(ecs_world_view_t(world, RendSettingsView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugRendPanelComp* panel  = ecs_view_write_t(itr, DebugRendPanelComp);
    UiCanvasComp*       canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(settingsItr, panel->window)) {
      continue; // Window has been destroyed, or has no render settings.
    }
    RendSettingsComp* settings = ecs_view_write_t(settingsItr, RendSettingsComp);

    ui_canvas_reset(canvas);
    rend_panel_draw(canvas, panel, settings);

    if (panel->state.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
  }
}

ecs_module_init(debug_rend_module) {
  ecs_register_comp(DebugRendPanelComp);

  ecs_register_view(RendSettingsView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugRendUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(RendSettingsView));
}

EcsEntityId debug_rend_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugRendPanelComp,
      .state  = ui_panel_init(ui_vector(250, 225)),
      .window = window);
  return panelEntity;
}
