#pragma once
#include "dev/forward.h"
#include "scene/level.h"

typedef struct {
  EcsEntityId    levelAsset;
  SceneLevelMode levelMode;
} DevLevelRequest;

ecs_comp_extern(DevLevelPanelComp);

EcsEntityId dev_level_panel_open(EcsWorld*, EcsEntityId window, DevPanelType);

bool dev_level_consume_request(DevLevelPanelComp*, DevLevelRequest* out);
