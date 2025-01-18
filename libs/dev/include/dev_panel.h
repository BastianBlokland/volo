#pragma once
#include "dev.h"

typedef enum eDebugPanelType {
  DebugPanelType_Normal,
  DebugPanelType_Detached,
} DebugPanelType;

ecs_comp_extern(DebugPanelComp);

DebugPanelType dev_panel_type(const DebugPanelComp*);

void dev_panel_hide(DebugPanelComp*, bool hide);
bool dev_panel_hidden(const DebugPanelComp*);

EcsEntityId dev_panel_create(EcsWorld*, EcsEntityId window, DebugPanelType);
