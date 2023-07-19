#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

ecs_comp_extern(InputStateComp);

void         input_camera_center(InputStateComp*, GeoVector worldPos);
EcsEntityId  input_hovered_entity(const InputStateComp*);
TimeDuration input_hovered_time(const InputStateComp*);
