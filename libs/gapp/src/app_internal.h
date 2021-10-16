#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(GAppAppComp);

EcsEntityId gapp_app_create(EcsWorld*);
