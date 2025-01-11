#pragma once
#include "debug.h"

typedef enum eDebugPanelType {
  DebugPanelType_Normal,
  DebugPanelType_Detached,
} DebugPanelType;

ecs_comp_extern(DebugPanelComp);

DebugPanelType debug_panel_type(const DebugPanelComp*);

void debug_panel_hide(DebugPanelComp*, bool hide);
bool debug_panel_hidden(const DebugPanelComp*);

EcsEntityId debug_panel_create(EcsWorld*, EcsEntityId window, DebugPanelType);
