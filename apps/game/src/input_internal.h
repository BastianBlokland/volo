#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(InputStateComp);

EcsEntityId  input_hovered_entity(const InputStateComp*);
TimeDuration input_hovered_time(const InputStateComp*);
