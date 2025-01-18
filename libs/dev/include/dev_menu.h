#pragma once
#include "dev.h"

ecs_comp_extern(DevMenuComp);

EcsEntityId dev_menu_create(EcsWorld*, EcsEntityId window);
