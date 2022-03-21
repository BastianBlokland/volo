#include "debug_interface.h"
#include "ecs_world.h"
#include "ui.h"

// clang-format off

static const String g_tooltipPlaceholder  = string_static("Placeholder tooltip.");

// clang-format on

ecs_comp_define(DebugInterfacePanelComp) {
  UiPanelState state;
  EcsEntityId  window;
};

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugInterfacePanelComp);
  ecs_access_write(UiCanvasComp);
}

static void interface_panel_draw(UiCanvasComp* canvas, DebugInterfacePanelComp* panel) {
  const String title = fmt_write_scratch("{} Interface Settings", fmt_ui_shape(FormatShapes));
  ui_panel_begin(canvas, &panel->state, .title = title);

  UiGridState layoutGrid = ui_grid_init(canvas, .size = {140, 25});

  ui_label(canvas, string_lit("Placeholder"));
  ui_grid_next_col(canvas, &layoutGrid);
  f32 placeholder = 0;
  ui_slider(canvas, &placeholder, .tooltip = g_tooltipPlaceholder);
  ui_grid_next_row(canvas, &layoutGrid);

  ui_panel_end(canvas, &panel->state);
}

ecs_system_define(DebugInterfaceUpdatePanelSys) {

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugInterfacePanelComp* panel  = ecs_view_write_t(itr, DebugInterfacePanelComp);
    UiCanvasComp*            canvas = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    interface_panel_draw(canvas, panel);

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

  ecs_register_view(PanelUpdateView);

  ecs_register_system(DebugInterfaceUpdatePanelSys, ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_interface_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugInterfacePanelComp,
      .state  = ui_panel_init(ui_vector(310, 250)),
      .window = window);
  return panelEntity;
}
