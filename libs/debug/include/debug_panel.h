#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(DebugPanelComp);

void debug_panel_hide(DebugPanelComp*, bool hidden);
bool debug_panel_hidden(const DebugPanelComp*);

EcsEntityId debug_panel_create(EcsWorld*, EcsEntityId window);
