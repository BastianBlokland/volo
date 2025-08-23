#pragma once
#include "asset/forward.h"
#include "ecs/forward.h"

enum {
  AppOrder_HudDraw = 725,
};

void hud_init(EcsWorld*, AssetManagerComp*, EcsEntityId cameraEntity);
