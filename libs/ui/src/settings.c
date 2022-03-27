#include "ui_settings.h"

ecs_comp_define_public(UiSettingsComp);

ecs_module_init(ui_settings_module) { ecs_register_comp(UiSettingsComp); }

void ui_settings_to_default(UiSettingsComp* settings) {
  settings->flags            = 0;
  settings->scale            = 1.0f;
  settings->defaultColor     = ui_color_white;
  settings->defaultOutline   = 1;
  settings->defaultVariation = 0;
  settings->defaultWeight    = UiWeight_Normal;
}
