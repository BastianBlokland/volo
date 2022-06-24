#include "debug_animation.h"
#include "debug_register.h"
#include "ecs_world.h"
#include "ui.h"

ecs_comp_define(DebugAnimationPanelComp) { UiPanel panel; };

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugAnimationPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void animation_panel_draw(UiCanvasComp* canvas, DebugAnimationPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Animation Debug", fmt_ui_shape(Animation));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugAnimationUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugAnimationPanelComp* panelComp = ecs_view_write_t(itr, DebugAnimationPanelComp);
    UiCanvasComp*            canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    animation_panel_draw(canvas, panelComp);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_animation_module) {
  ecs_register_comp(DebugAnimationPanelComp);

  ecs_register_view(PanelUpdateView);

  ecs_register_system(DebugAnimationUpdatePanelSys, ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_animation_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugAnimationPanelComp, .panel = ui_panel(ui_vector(450, 300)));
  return panelEntity;
}
