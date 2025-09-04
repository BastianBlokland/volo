#pragma once
#include "ecs/module.h"

typedef enum {
  GameQuality_VeryLow,
  GameQuality_Low,
  GameQuality_Medium,
  GameQuality_High,

  GameQuality_Count,
} GameQuality;

typedef enum {
  GameUiScale_Small,
  GameUiScale_Normal,
  GameUiScale_Big,

  GameUiScale_Count,
} GameUiScale;

extern const String g_gameQualityLabels[GameQuality_Count]; // Localization keys.
extern const String g_gameUiScaleLabels[GameUiScale_Count]; // Localization keys.

ecs_comp_extern_public(GamePrefsComp) {
  bool        dirty; // Indicates that the preference file should be saved to disk.
  f32         volume;
  bool        powerSaving;
  bool        fullscreen;
  u16         windowWidth, windowHeight;
  GameQuality quality;
  GameUiScale uiScale;
  String      locale; // For example 'en-us'.
};

GamePrefsComp* game_prefs_init(EcsWorld*);
void           game_prefs_locale_set(GamePrefsComp*, String locale);
