#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(SceneFactionComp) { u32 id; };

bool scene_is_friendly(const SceneFactionComp*, const SceneFactionComp*);
bool scene_is_hostile(const SceneFactionComp*, const SceneFactionComp*);
