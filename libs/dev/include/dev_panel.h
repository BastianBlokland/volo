#pragma once
#include "dev.h"

typedef enum eDevPanelType {
  DevPanelType_Normal,
  DevPanelType_Detached,
} DevPanelType;

ecs_comp_extern(DevPanelComp);

DevPanelType dev_panel_type(const DevPanelComp*);

void dev_panel_hide(DevPanelComp*, bool hide);
bool dev_panel_hidden(const DevPanelComp*);

EcsEntityId dev_panel_create(EcsWorld*, EcsEntityId window, DevPanelType);
