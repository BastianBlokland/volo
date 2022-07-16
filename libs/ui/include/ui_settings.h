#pragma once
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_units.h"

typedef enum {
  UiSettingFlags_DpiScaling     = 1 << 0,
  UiSettingFlags_DebugShading   = 1 << 1,
  UiSettingFlags_DebugInspector = 1 << 2,
} UiSettingFlags;

ecs_comp_extern_public(UiSettingsComp) {
  UiSettingFlags flags;
  f32            scale;
  UiColor        defaultColor;
  u8             defaultOutline;
  u8             defaultVariation;
  UiWeight       defaultWeight : 8;
};

void ui_settings_to_default(UiSettingsComp*);
