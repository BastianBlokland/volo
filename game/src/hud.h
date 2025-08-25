#pragma once
#include "asset/forward.h"
#include "ecs/forward.h"

enum {
  GameOrder_HudDraw = 725,
};

void game_hud_init(EcsWorld*, AssetManagerComp*, EcsEntityId cameraEntity);
