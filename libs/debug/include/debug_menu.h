#pragma once
#include "ecs_module.h"

ecs_comp_extern(DebugMenuComp);

EcsEntityId debug_menu_create(EcsWorld*, EcsEntityId window);
