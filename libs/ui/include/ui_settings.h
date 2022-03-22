#pragma once
#include "ecs_module.h"

typedef enum {
  UiSettingFlags_DebugShading = 1 << 0,
} UiSettingFlags;

ecs_comp_extern_public(UiSettingsComp) {
  UiSettingFlags flags;
  f32            scale;
};

void ui_settings_to_default(UiSettingsComp*);
