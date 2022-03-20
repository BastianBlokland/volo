#include "core_math.h"
#include "debug_camera.h"
#include "ecs_world.h"
#include "scene_camera.h"
#include "ui.h"

static const String g_tooltipFov = string_static("Perspective field of view in degrees");

ecs_comp_define(DebugCameraPanelComp) {
  UiPanelState state;
  EcsEntityId  window;
};

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugCameraPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(WindowView) { ecs_access_write(SceneCameraComp); }

static void
camera_panel_draw(UiCanvasComp* canvas, DebugCameraPanelComp* panel, SceneCameraComp* camera) {
  const String title = fmt_write_scratch("{} Camera Settings", fmt_ui_shape(PhotoCamera));
  ui_panel_begin(canvas, &panel->state, .title = title);

  UiGridState layoutGrid = ui_grid_init(canvas, .size = {110, 25});

  ui_label(canvas, string_lit("Field of view"));
  ui_grid_next_col(canvas, &layoutGrid);
  f32 fovDegrees = camera->persFov * math_rad_to_deg;
  if (ui_slider(canvas, &fovDegrees, .min = 10, .max = 150, .tooltip = g_tooltipFov)) {
    camera->persFov = fovDegrees * math_deg_to_rad;
  }
  ui_grid_next_row(canvas, &layoutGrid);

  ui_panel_end(canvas, &panel->state);
}

ecs_system_define(DebugCameraUpdatePanelSys) {
  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, WindowView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugCameraPanelComp* panel  = ecs_view_write_t(itr, DebugCameraPanelComp);
    UiCanvasComp*         canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, panel->window)) {
      continue; // Window has been destroyed, or has no camera.
    }
    SceneCameraComp* camera = ecs_view_write_t(windowItr, SceneCameraComp);

    ui_canvas_reset(canvas);
    camera_panel_draw(canvas, panel, camera);

    if (panel->state.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_camera_module) {
  ecs_register_comp(DebugCameraPanelComp);

  ecs_register_view(PanelUpdateView);
  ecs_register_view(WindowView);

  ecs_register_system(
      DebugCameraUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(WindowView));
}

EcsEntityId debug_camera_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugCameraPanelComp,
      .state  = ui_panel_init(ui_vector(250, 150)),
      .window = window);
  return panelEntity;
}
