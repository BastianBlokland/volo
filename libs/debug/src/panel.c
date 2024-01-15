#include "debug_panel.h"
#include "ecs_world.h"
#include "ui_canvas.h"

ecs_comp_define_public(DebugPanelComp);

ecs_module_init(debug_panel_module) { ecs_register_comp_empty(DebugPanelComp); }

EcsEntityId debug_panel_create(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_empty_t(world, panelEntity, DebugPanelComp);
  return panelEntity;
}
