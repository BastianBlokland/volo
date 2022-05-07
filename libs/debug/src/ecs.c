#include "debug_ecs.h"
#include "debug_register.h"
#include "ecs_world.h"
#include "ui.h"

ecs_comp_define(DebugEcsPanelComp) {
  UiPanel     panel;
  EcsEntityId window;
};

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugEcsPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void physics_panel_draw(UiCanvasComp* canvas, DebugEcsPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Ecs Debug", fmt_ui_shape(Extension));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugPhysicsUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId  entity    = ecs_view_entity(itr);
    DebugEcsPanelComp* panelComp = ecs_view_write_t(itr, DebugEcsPanelComp);
    UiCanvasComp*      canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    physics_panel_draw(canvas, panelComp);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_ecs_module) {
  ecs_register_comp(DebugEcsPanelComp);

  ecs_register_view(PanelUpdateView);

  ecs_register_system(DebugPhysicsUpdatePanelSys, ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_ecs_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugEcsPanelComp,
      .panel  = ui_panel(ui_vector(330, 255)),
      .window = window);
  return panelEntity;
}
