#pragma once
#include "asset/forward.h"
#include "ecs/forward.h"

enum {
  GameOrder_HudDraw = 725,
};

typedef enum {
  GameHudAction_Pause,
} GameHudAction;

ecs_comp_extern(GameHudComp);

GameHudComp* game_hud_init(EcsWorld*, AssetManagerComp*, EcsEntityId cameraEntity);
bool         game_hud_consume_action(GameHudComp*, GameHudAction);
