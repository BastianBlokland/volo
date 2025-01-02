#pragma once
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_units.h"

typedef enum eUiSettingGlobalFlags {
  UiSettingGlobal_DpiScaling   = 1 << 0,
  UiSettingGlobal_DebugShading = 1 << 1,
} UiSettingGlobalFlags;

typedef enum eUiInspectorMode {
  UiInspectorMode_None,
  UiInspectorMode_DebugInteractables,
  UiInspectorMode_DebugAll,

  UiInspectorMode_Count,
} UiInspectorMode;

ecs_comp_extern_public(UiSettingsGlobalComp) {
  UiSettingGlobalFlags flags;
  UiInspectorMode      inspectorMode;
  f32                  scale;
  UiColor              defaultColor;
  u8                   defaultOutline;
  u8                   defaultVariation;
  UiWeight             defaultWeight : 8;
};

void ui_settings_global_to_default(UiSettingsGlobalComp*);
