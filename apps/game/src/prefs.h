#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(GamePrefsComp) {
  bool dirty; // Indicates that the preference file should be saved to disk.
  f32  volume;
  bool powerSaving;
  bool fullscreen;
  u16  windowWidth, windowHeight;
};

GamePrefsComp* prefs_init(EcsWorld*);
