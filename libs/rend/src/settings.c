#include "ecs_world.h"
#include "rend_register.h"
#include "rend_settings.h"

ecs_comp_define_public(RendSettingsComp);
ecs_comp_define_public(RendGlobalSettingsComp);

ecs_module_init(rend_settings_module) {
  ecs_register_comp(RendSettingsComp);
  ecs_register_comp(RendGlobalSettingsComp);
}

void rend_settings_to_default(RendSettingsComp* settings) {
  settings->flags           = RendFlags_FrustumCulling;
  settings->presentMode     = RendPresentMode_VSyncRelaxed;
  settings->resolutionScale = 1.0f;
}

void rend_global_settings_to_default(RendGlobalSettingsComp* settings) {
  settings->flags       = 0;
  settings->limiterFreq = 60;
}
