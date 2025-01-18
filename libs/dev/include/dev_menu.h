#pragma once
#include "dev.h"

ecs_comp_extern(DevMenuComp);

EcsEntityId debug_menu_create(EcsWorld*, EcsEntityId window);
