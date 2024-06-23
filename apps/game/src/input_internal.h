#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

ecs_comp_extern(InputStateComp);

void input_camera_center(InputStateComp*, GeoVector worldPos);
void input_set_allow_zoom_over_ui(InputStateComp*, bool allowZoomOverUI);
bool input_hovered_entity(const InputStateComp*, EcsEntityId* outEntity, TimeDuration* outTime);
