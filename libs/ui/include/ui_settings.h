#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(UiSettingsComp) { f32 scale; };

void ui_settings_to_default(UiSettingsComp*);
