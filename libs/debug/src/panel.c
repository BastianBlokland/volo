#include "debug_panel.h"
#include "ecs_world.h"
#include "ui_canvas.h"

ecs_comp_define(DebugPanelComp) {
  DebugPanelType type;
  bool           hidden;
};

ecs_module_init(debug_panel_module) { ecs_register_comp(DebugPanelComp); }

DebugPanelType debug_panel_type(const DebugPanelComp* panel) { return panel->type; }

void debug_panel_hide(DebugPanelComp* panel, const bool hide) { panel->hidden = hide; }

bool debug_panel_hidden(const DebugPanelComp* panel) {
  // NOTE: Detached panels cannot be hidden.
  return panel->type == DebugPanelType_Normal && panel->hidden;
}

EcsEntityId
debug_panel_create(EcsWorld* world, const EcsEntityId window, const DebugPanelType type) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(world, panelEntity, DebugPanelComp, .type = type);
  return panelEntity;
}
