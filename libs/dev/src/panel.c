#include "dev_panel.h"
#include "ecs_world.h"
#include "ui_canvas.h"

ecs_comp_define(DevPanelComp) {
  DevPanelType type;
  bool         hidden;
};

ecs_module_init(dev_panel_module) { ecs_register_comp(DevPanelComp); }

DevPanelType dev_panel_type(const DevPanelComp* panel) { return panel->type; }

void dev_panel_hide(DevPanelComp* panel, const bool hide) { panel->hidden = hide; }

bool dev_panel_hidden(const DevPanelComp* panel) {
  // NOTE: Detached panels cannot be hidden.
  return panel->type == DevPanelType_Normal && panel->hidden;
}

EcsEntityId dev_panel_create(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(world, panelEntity, DevPanelComp, .type = type);
  return panelEntity;
}
