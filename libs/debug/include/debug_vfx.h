#pragma once
#include "debug_panel.h"
#include "ecs_entity.h"
#include "ecs_module.h"

EcsEntityId debug_vfx_panel_open(EcsWorld*, EcsEntityId window, DebugPanelType);
