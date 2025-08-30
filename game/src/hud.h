#pragma once
#include "asset/forward.h"
#include "ecs/forward.h"

enum {
  GameOrder_HudDraw = 725,
};

ecs_comp_extern(GameHudComp);

GameHudComp* game_hud_init(EcsWorld*, AssetManagerComp*, EcsEntityId cameraEntity);
bool         game_hud_consume_pause(GameHudComp*); // Returns true if pause was requested.
