#include "ecs_world.h"
#include "rend_register.h"
#include "rend_settings.h"

ecs_comp_define_public(RendSettingsComp);

ecs_module_init(rend_settings_module) { ecs_register_comp(RendSettingsComp); }
