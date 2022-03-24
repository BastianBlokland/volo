#pragma once
#include "ecs_module.h"
#include "ui_color.h"

typedef enum {
  UiSettingFlags_DebugShading   = 1 << 0,
  UiSettingFlags_DebugInspector = 1 << 1,
} UiSettingFlags;

ecs_comp_extern_public(UiSettingsComp) {
  UiSettingFlags flags;
  f32            scale;
  UiColor        defaultColor;
  u8             defaultOutline;
  u8             defaultVariation;
};

void ui_settings_to_default(UiSettingsComp*);
