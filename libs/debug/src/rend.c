#include "ecs_world.h"
#include "rend_reset.h"
#include "rend_settings.h"
#include "ui.h"

// clang-format off

static const String g_tooltipVSync          = string_static("Should presentation wait for VBlanks?");
static const String g_tooltipScale          = string_static("Render resolution scale.");
static const String g_tooltipLimiter        = string_static("Frame frequency limiter (in hz).\nNote: 0 disables the limiter.");
static const String g_tooltipFrustumCulling = string_static("Should draws be culled if their bounds are outside of the view frustum?");
static const String g_tooltipValidation     = string_static("Should gpu api validation be enabled?\nNote: Requires a reset to take effect.");
static const String g_tooltipVerbose        = string_static("Should verbose logging be enabled?");
static const String g_tooltipDefaults       = string_static("Reset all settings to their defaults.");
static const String g_tooltipReset          = string_static("Re-initialize the renderer.");

// clang-format on

ecs_comp_define(DebugRendPanelComp) {
  UiPanelState state;
  EcsEntityId  window;
};

ecs_view_define(GlobalView) { ecs_access_write(RendGlobalSettingsComp); }
ecs_view_define(WindowView) { ecs_access_write(RendSettingsComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugRendPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void rend_panel_draw(
    EcsWorld*               world,
    UiCanvasComp*           canvas,
    DebugRendPanelComp*     panel,
    RendSettingsComp*       settings,
    RendGlobalSettingsComp* globalSettings) {

  const String title = fmt_write_scratch("{} Renderer Settings", fmt_ui_shape(Brush));
  ui_panel_begin(canvas, &panel->state, .title = title);

  UiGridState layoutGrid = ui_grid_init(canvas, .size = {140, 25});

  ui_label(canvas, string_lit("VSync"));
  ui_grid_next_col(canvas, &layoutGrid);
  bool vsync = settings->presentMode == RendPresentMode_VSyncRelaxed;
  if (ui_toggle(canvas, &vsync, .tooltip = g_tooltipVSync)) {
    settings->presentMode = vsync ? RendPresentMode_VSyncRelaxed : RendPresentMode_Immediate;
  }
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Limiter"));
  ui_grid_next_col(canvas, &layoutGrid);
  f32 limiterFreq = globalSettings->limiterFreq;
  if (ui_slider(
          canvas, &limiterFreq, .min = 0, .max = 240, .step = 30, .tooltip = g_tooltipLimiter)) {
    globalSettings->limiterFreq = (u16)limiterFreq;
  }
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Scale"));
  ui_grid_next_col(canvas, &layoutGrid);
  ui_slider(
      canvas,
      &settings->resolutionScale,
      .min     = 0.2f,
      .max     = 2.0f,
      .step    = 0.1f,
      .tooltip = g_tooltipScale);
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Frustum culling"));
  ui_grid_next_col(canvas, &layoutGrid);
  bool frustumCulling = (settings->flags & RendFlags_FrustumCulling) != 0;
  if (ui_toggle(canvas, &frustumCulling, .tooltip = g_tooltipFrustumCulling)) {
    settings->flags ^= RendFlags_FrustumCulling;
  }
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Validation"));
  ui_grid_next_col(canvas, &layoutGrid);
  bool validation = (globalSettings->flags & RendGlobalFlags_Validation) != 0;
  if (ui_toggle(canvas, &validation, .tooltip = g_tooltipValidation)) {
    globalSettings->flags ^= RendGlobalFlags_Validation;
  }
  ui_grid_next_row(canvas, &layoutGrid);

  ui_label(canvas, string_lit("Verbose"));
  ui_grid_next_col(canvas, &layoutGrid);
  bool verbose = (globalSettings->flags & RendGlobalFlags_Verbose) != 0;
  if (ui_toggle(canvas, &verbose, .tooltip = g_tooltipVerbose)) {
    globalSettings->flags ^= RendGlobalFlags_Verbose;
  }
  ui_grid_next_row(canvas, &layoutGrid);

  if (ui_button(canvas, .label = string_lit("Defaults"), .tooltip = g_tooltipDefaults)) {
    rend_settings_to_default(settings);
    rend_global_settings_to_default(globalSettings);
  }
  ui_grid_next_col(canvas, &layoutGrid);
  if (ui_button(
          canvas,
          .label      = string_lit("Reset"),
          .frameColor = ui_color(255, 0, 0, 192),
          .tooltip    = g_tooltipReset)) {
    rend_reset(world);
  }

  ui_panel_end(canvas, &panel->state);
}

ecs_system_define(DebugRendUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  RendGlobalSettingsComp* globalSettings = ecs_view_write_t(globalItr, RendGlobalSettingsComp);

  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, WindowView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugRendPanelComp* panel  = ecs_view_write_t(itr, DebugRendPanelComp);
    UiCanvasComp*       canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, panel->window)) {
      continue; // Window has been destroyed, or has no render settings.
    }
    RendSettingsComp* settings = ecs_view_write_t(windowItr, RendSettingsComp);

    ui_canvas_reset(canvas);
    rend_panel_draw(world, canvas, panel, settings, globalSettings);

    if (panel->state.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_rend_module) {
  ecs_register_comp(DebugRendPanelComp);

  ecs_register_view(GlobalView);
  ecs_register_view(WindowView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugRendUpdatePanelSys,
      ecs_view_id(PanelUpdateView),
      ecs_view_id(WindowView),
      ecs_view_id(GlobalView));
}

EcsEntityId debug_rend_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugRendPanelComp,
      .state  = ui_panel_init(ui_vector(310, 255)),
      .window = window);
  return panelEntity;
}
