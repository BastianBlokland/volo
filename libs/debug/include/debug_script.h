#pragma once
#include "debug_panel.h"
#include "ecs_entity.h"

EcsEntityId debug_script_panel_open(EcsWorld*, EcsEntityId window, DebugPanelType);
EcsEntityId debug_script_panel_open_output(EcsWorld*, EcsEntityId window);
