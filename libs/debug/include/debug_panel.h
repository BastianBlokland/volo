#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(DebugPanelComp);

EcsEntityId debug_panel_create(EcsWorld*, EcsEntityId window);
