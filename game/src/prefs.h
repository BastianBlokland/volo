#pragma once
#include "ecs/module.h"

typedef enum {
  GameQuality_VeryLow,
  GameQuality_Low,
  GameQuality_Medium,
  GameQuality_High,

  GameQuality_Count,
} GameQuality;

extern const String g_gameQualityLabels[GameQuality_Count];

ecs_comp_extern_public(GamePrefsComp) {
  bool        dirty; // Indicates that the preference file should be saved to disk.
  f32         volume;
  bool        powerSaving;
  bool        fullscreen;
  u16         windowWidth, windowHeight;
  GameQuality quality;
  String      locale; // For example 'en-us'.
};

GamePrefsComp* game_prefs_init(EcsWorld*);
