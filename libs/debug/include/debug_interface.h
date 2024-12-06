#pragma once
#include "debug_panel.h"
#include "ecs_entity.h"

EcsEntityId debug_interface_panel_open(EcsWorld*, EcsEntityId window, DebugPanelType);
