#include "core_math.h"
#include "ecs_world.h"
#include "rend_register.h"
#include "rend_settings.h"

#define VOLO_REND_GPU_DEBUG 0

ecs_comp_define_public(RendSettingsComp);
ecs_comp_define_public(RendSettingsGlobalComp);

ecs_module_init(rend_settings_module) {
  ecs_register_comp(RendSettingsComp);
  ecs_register_comp(RendSettingsGlobalComp);
}

void rend_settings_to_default(RendSettingsComp* s) {
  s->flags                 = RendFlags_FrustumCulling | RendFlags_AmbientOcclusion;
  s->presentMode           = RendPresentMode_VSyncRelaxed;
  s->resolutionScale       = 1.0f;
  s->ambientOcclusionScale = 0.5f;
  s->shadowResolution      = 2048;
}

void rend_settings_global_to_default(RendSettingsGlobalComp* s) {
  s->flags       = RendGlobalFlags_SunShadows;
  s->limiterFreq = 0;

#if VOLO_REND_GPU_DEBUG
  s->flags |= RendGlobalFlags_Validation | RendGlobalFlags_DebugGpu | RendGlobalFlags_Verbose;
#endif

  s->lightSunRadiance = geo_color(1.0f, 0.9f, 0.8f, 2.0f);
  s->lightSunRotation = geo_quat_from_euler(geo_vector_mul(geo_vector(55, 15, 0), math_deg_to_rad));
  s->lightAmbient     = 0.25f;
}
