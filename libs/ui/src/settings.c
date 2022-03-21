#include "ui_settings.h"

ecs_comp_define_public(UiSettingsComp);

ecs_module_init(ui_settings_module) { ecs_register_comp(UiSettingsComp); }

void ui_settings_to_default(UiSettingsComp* settings) { settings->scale = 1.0f; }
