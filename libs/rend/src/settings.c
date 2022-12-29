#include "core_math.h"
#include "ecs_world.h"
#include "rend_register.h"
#include "rend_settings.h"

ecs_comp_define_public(RendSettingsComp);
ecs_comp_define_public(RendSettingsGlobalComp);

ecs_module_init(rend_settings_module) {
  ecs_register_comp(RendSettingsComp);
  ecs_register_comp(RendSettingsGlobalComp);
}

void rend_settings_to_default(RendSettingsComp* s) {
  s->flags           = RendFlags_FrustumCulling;
  s->presentMode     = RendPresentMode_VSyncRelaxed;
  s->resolutionScale = 1.0f;
}

void rend_settings_global_to_default(RendSettingsGlobalComp* s) {
  s->flags       = 0;
  s->limiterFreq = 0;

  s->lightSunRadiance = geo_color(1.0f, 0.9f, 0.8f, 3.0f);
  s->lightSunRotation = geo_quat_from_euler(geo_vector_mul(geo_vector(50, 15, 0), math_deg_to_rad));
  s->lightAmbient     = 0.1f;
}
