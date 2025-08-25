#pragma once
#include "core/forward.h"
#include "ecs/module.h"
#include "geo/vector.h"

ecs_comp_extern(GameInputComp);

void game_input_camera_center(GameInputComp*, GeoVector worldPos);
void game_input_set_allow_zoom_over_ui(GameInputComp*, bool allowZoomOverUI);
bool game_input_hovered_entity(const GameInputComp*, EcsEntityId* outEntity, TimeDuration* outTime);
