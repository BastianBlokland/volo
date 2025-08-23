#include "ui/settings.h"

ecs_comp_define(UiSettingsGlobalComp);

ecs_module_init(ui_settings_module) { ecs_register_comp(UiSettingsGlobalComp); }

void ui_settings_global_to_default(UiSettingsGlobalComp* s) {
  s->flags            = UiSettingGlobal_DpiScaling;
  s->scale            = 1.0f;
  s->defaultColor     = ui_color_white;
  s->defaultOutline   = 1;
  s->defaultVariation = 0;
  s->defaultWeight    = UiWeight_Normal;
}
