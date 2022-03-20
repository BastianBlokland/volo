#include "debug_camera.h"
#include "ecs_world.h"
#include "ui.h"

ecs_comp_define(DebugCameraPanelComp) {
  UiPanelState state;
  EcsEntityId  window;
};

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugCameraPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void camera_panel_draw(UiCanvasComp* canvas, DebugCameraPanelComp* panel) {
  const String title = fmt_write_scratch("{} Camera Settings", fmt_ui_shape(PhotoCamera));
  ui_panel_begin(canvas, &panel->state, .title = title);

  ui_label(canvas, string_lit("Placeholder"));

  ui_panel_end(canvas, &panel->state);
}

ecs_system_define(DebugCameraUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugCameraPanelComp* panel  = ecs_view_write_t(itr, DebugCameraPanelComp);
    UiCanvasComp*         canvas = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    camera_panel_draw(canvas, panel);

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

  ecs_register_system(DebugCameraUpdatePanelSys, ecs_view_id(PanelUpdateView));
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
