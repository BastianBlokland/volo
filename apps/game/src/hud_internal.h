#pragma once
#include "ecs_world.h"

// Forward declare from 'asset_manager.h'.
ecs_comp_extern(AssetManagerComp);

void hud_init(EcsWorld*, AssetManagerComp*, EcsEntityId cameraEntity);
