#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(GamePrefsComp) { f32 volume; };

GamePrefsComp* prefs_init(EcsWorld*);
void           prefs_save(const GamePrefsComp*);
