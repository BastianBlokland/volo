#pragma once
#include "debug_panel.h"
#include "ecs_entity.h"
#include "ecs_module.h"

EcsEntityId debug_inspector_panel_open(EcsWorld*, EcsEntityId window, DebugPanelType);
