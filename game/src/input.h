#pragma once
#include "core/forward.h"
#include "ecs/module.h"
#include "geo/vector.h"

enum {
  GameOrder_Input   = -1,
  GameOrder_InputUi = 1,
};

typedef enum {
  GameInputType_None,
  GameInputType_Normal,
  GameInputType_FreeCamera,
} GameInputType;

ecs_comp_extern(GameInputComp);

GameInputType game_input_type(const GameInputComp*);
void          game_input_type_set(GameInputComp*, GameInputType);

void game_input_camera_center(GameInputComp*, GeoVector worldPos);
void game_input_set_allow_zoom_over_ui(GameInputComp*, bool allowZoomOverUI);
bool game_input_hovered_entity(const GameInputComp*, EcsEntityId* outEntity, TimeDuration* outTime);
