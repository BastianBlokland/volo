#include "debug_panel.h"
#include "ecs_world.h"
#include "ui_canvas.h"

ecs_comp_define(DebugPanelComp) { bool hidden; };

ecs_module_init(debug_panel_module) { ecs_register_comp(DebugPanelComp); }

void debug_panel_hide(DebugPanelComp* panel, const bool hidden) { panel->hidden = hidden; }

bool debug_panel_hidden(const DebugPanelComp* panel) { return panel->hidden; }

EcsEntityId debug_panel_create(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(world, panelEntity, DebugPanelComp);
  return panelEntity;
}
