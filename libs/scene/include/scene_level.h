#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(SceneLevelManagerComp);

bool        scene_level_is_loading(const SceneLevelManagerComp*);
EcsEntityId scene_level_current(const SceneLevelManagerComp*);
void        scene_level_load(EcsWorld*, EcsEntityId levelAsset);
void        scene_level_reload(EcsWorld*);
void        scene_level_save(EcsWorld*, EcsEntityId levelAsset);
