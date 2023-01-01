#include "core_alloc.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "rend_register.h"
#include "rend_settings.h"

#define VOLO_REND_GPU_DEBUG 0

ecs_comp_define_public(RendSettingsComp);
ecs_comp_define_public(RendSettingsGlobalComp);

static void ecs_destruct_rend_settings_comp(void* data) {
  RendSettingsComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->aoKernel, rend_ao_kernel_size);
}

ecs_module_init(rend_settings_module) {
  ecs_register_comp(RendSettingsComp, .destructor = ecs_destruct_rend_settings_comp);
  ecs_register_comp(RendSettingsGlobalComp);
}

void rend_settings_to_default(RendSettingsComp* s) {
  s->flags             = RendFlags_FrustumCulling | RendFlags_AmbientOcclusion;
  s->presentMode       = RendPresentMode_VSyncRelaxed;
  s->composeMode       = RendComposeMode_Normal;
  s->resolutionScale   = 1.0f;
  s->aoAngle           = 80 * math_deg_to_rad;
  s->aoRadius          = 0.15f;
  s->aoPower           = 1.75f;
  s->aoResolutionScale = 0.5f;
  s->shadowResolution  = 2048;

  rend_settings_generate_ao_kernel(s);
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

void rend_settings_generate_ao_kernel(RendSettingsComp* s) {
  if (!s->aoKernel) {
    s->aoKernel = alloc_array_t(g_alloc_heap, GeoVector, rend_ao_kernel_size);
  }
  Rng* rng = rng_create_xorwow(g_alloc_scratch, 42);
  for (u32 i = 0; i != rend_ao_kernel_size; ++i) {
    const GeoVector randInCone = geo_vector_rand_in_cone3(rng, s->aoAngle);
    const f32       rand       = rng_sample_f32(rng);
    const f32       mag        = math_lerp(0.1f, 1.0f, rand * rand) * s->aoRadius;
    s->aoKernel[i]             = geo_vector_mul(randInCone, mag);
  }
}
