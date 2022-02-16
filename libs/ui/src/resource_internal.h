#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(UiGlobalResourcesComp);

EcsEntityId ui_resource_font(const UiGlobalResourcesComp*);
EcsEntityId ui_resource_graphic(const UiGlobalResourcesComp*);
