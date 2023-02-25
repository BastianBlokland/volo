#pragma once
#include "ecs_module.h"

ecs_comp_extern(SceneLevelManagerComp);

bool   scene_level_is_loading(const SceneLevelManagerComp*);
String scene_level_current_id(const SceneLevelManagerComp*);
void   scene_level_load(EcsWorld*, String levelId);
void   scene_level_save(EcsWorld*, String levelId);
