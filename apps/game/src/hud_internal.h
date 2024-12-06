#pragma once
#include "asset.h"
#include "ecs.h"

enum {
  AppOrder_HudDraw = 725,
};

void hud_init(EcsWorld*, AssetManagerComp*, EcsEntityId cameraEntity);
