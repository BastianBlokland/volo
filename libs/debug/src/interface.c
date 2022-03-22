#include "debug_interface.h"
#include "ecs_world.h"
#include "ui.h"
#include "ui_settings.h"

// clang-format off

static const String g_tooltipScale        = string_static("User interface scaling factor.\nNote: Needs to be applied before taking effect.");
static const String g_tooltipDebugShading = string_static("Enable the debug shading.\n\n"
                                                          "Meaning:\n"
                                                          "- \a#001CFFFF\a|01Blue\ar: Dark is fully inside the shape and light is on the shape's outer edge.\n"
                                                          "- \a#FFFFFFFF\a|01White\ar: The shape's outline.\n"
                                                          "- \a#00FF00FF\a|01Green\ar: Dark is on the shape's outer edge and light is fully outside the shape.\n");
static const String g_tooltipApply        = string_static("Apply outstanding interface setting changes.");
static const String g_tooltipDefaults     = string_static("Reset all settings to their defaults.");

// clang-format on

ecs_comp_define(DebugInterfacePanelComp) {
  UiPanelState state;
  EcsEntityId  window;
  f32          newScale;
};

ecs_view_define(WindowView) { ecs_access_write(UiSettingsComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugInterfacePanelComp);
  ecs_access_write(UiCanvasComp);
}

static void interface_panel_draw(
    UiCanvasComp* canvas, DebugInterfacePanelComp* panel, UiSettingsComp* settings) {

  const String title = fmt_write_scratch("{} Interface Settings", fmt_ui_shape(FormatShapes));
  ui_panel_begin(canvas, &panel->state, .title = title);

  UiGridState layoutGrid = ui_grid_init(canvas, .size = {140, 25});

  bool dirty = false;
  dirty |= panel->newScale != settings->scale;

  ui_label(canvas, string_lit("Scale factor"));
  ui_grid_next_col(canvas, &layoutGrid);
  ui_slider(canvas, &panel->newScale, .min = 0.5, .max = 2, .tooltip = g_tooltipScale);
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Debug shading"));
  ui_grid_next_col(canvas, &layoutGrid);
  bool debugShading = (settings->flags & UiSettingFlags_DebugShading) != 0;
  if (ui_toggle(canvas, &debugShading, .tooltip = g_tooltipDebugShading)) {
    settings->flags ^= UiSettingFlags_DebugShading;
  }
  ui_grid_next_row(canvas, &layoutGrid);

  if (ui_button(canvas, .label = string_lit("Defaults"), .tooltip = g_tooltipDefaults)) {
    ui_settings_to_default(settings);
    panel->newScale = settings->scale;
  }
  ui_grid_next_col(canvas, &layoutGrid);
  if (ui_button(
          canvas,
          .label      = string_lit("Apply"),
          .frameColor = dirty ? ui_color(0, 178, 0, 192) : ui_color(32, 32, 32, 192),
          .flags      = dirty ? 0 : UiWidget_Disabled,
          .tooltip    = g_tooltipApply)) {
    settings->scale = panel->newScale;
  }

  ui_panel_end(canvas, &panel->state);
}

ecs_system_define(DebugInterfaceUpdatePanelSys) {
  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, WindowView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugInterfacePanelComp* panel  = ecs_view_write_t(itr, DebugInterfacePanelComp);
    UiCanvasComp*            canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, panel->window)) {
      continue; // Window has been destroyed, or has no ui settings.
    }
    UiSettingsComp* settings = ecs_view_write_t(windowItr, UiSettingsComp);

    if (panel->newScale == 0) {
      panel->newScale = settings->scale;
    }

    ui_canvas_reset(canvas);
    interface_panel_draw(canvas, panel, settings);

    if (panel->state.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_interface_module) {
  ecs_register_comp(DebugInterfacePanelComp);

  ecs_register_view(WindowView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugInterfaceUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(WindowView));
}

EcsEntityId debug_interface_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugInterfacePanelComp,
      .state  = ui_panel_init(ui_vector(310, 175)),
      .window = window);
  return panelEntity;
}
