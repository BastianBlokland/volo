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
  alloc_free_array_t(g_allocHeap, comp->aoKernel, rend_ao_kernel_size);
}

ecs_module_init(rend_settings_module) {
  ecs_register_comp(RendSettingsComp, .destructor = ecs_destruct_rend_settings_comp);
  ecs_register_comp(RendSettingsGlobalComp);
}

RendSettingsGlobalComp* rend_settings_global_init(EcsWorld* world) {
  const EcsEntityId       global   = ecs_world_global(world);
  RendSettingsGlobalComp* settings = ecs_world_add_t(world, global, RendSettingsGlobalComp);
  rend_settings_global_to_default(settings);
  return settings;
}

RendSettingsComp* rend_settings_window_init(EcsWorld* world, const EcsEntityId window) {
  RendSettingsComp* settings = ecs_world_add_t(world, window, RendSettingsComp);
  rend_settings_to_default(settings);
  return settings;
}

void rend_settings_to_default(RendSettingsComp* s) {
  // clang-format off
  s->flags = RendFlags_FrustumCulling       |
             RendFlags_AmbientOcclusion     |
             RendFlags_AmbientOcclusionBlur |
             RendFlags_Shadows              |
             RendFlags_Bloom                |
             RendFlags_Distortion           |
             RendFlags_Decals               |
             RendFlags_VfxSpriteShadows;
  // clang-format on
  s->presentMode               = RendPresentMode_VSyncRelaxed;
  s->ambientMode               = RendAmbientMode_SpecularIrradiance;
  s->skyMode                   = RendSkyMode_None;
  s->exposure                  = 1.0f;
  s->tonemapper                = RendTonemapper_LinearSmooth;
  s->resolutionScale           = 1.0f;
  s->aoAngle                   = 80 * math_deg_to_rad;
  s->aoRadius                  = 0.5f;
  s->aoRadiusPower             = 2.5f;
  s->aoPower                   = 3.5f;
  s->aoResolutionScale         = 0.75f;
  s->shadowResolution          = 2048;
  s->fogResolution             = 512;
  s->fogBlurSteps              = 2;
  s->fogBlurScale              = 0.85f;
  s->bloomIntensity            = 0.04f;
  s->bloomSteps                = 5;
  s->bloomRadius               = 0.003f;
  s->distortionResolutionScale = 0.25f;
  s->debugViewerResource       = 0;
  s->debugViewerLod            = 0.0f;
  s->debugViewerFlags          = 0;

  rend_settings_generate_ao_kernel(s);
}

void rend_settings_global_to_default(RendSettingsGlobalComp* s) {
  s->flags       = RendGlobalFlags_Fog;
  s->limiterFreq = 0;

#if VOLO_REND_GPU_DEBUG
  s->flags |= RendGlobalFlags_Validation | RendGlobalFlags_DebugGpu;
#endif

  s->shadowFilterSize = 0.125f;
  s->fogDilation      = -3.0f;
}

void rend_settings_generate_ao_kernel(RendSettingsComp* s) {
  if (!s->aoKernel) {
    s->aoKernel = alloc_array_t(g_allocHeap, GeoVector, rend_ao_kernel_size);
  }
  Rng* rng = rng_create_xorwow(g_allocScratch, 42);
  for (u32 i = 0; i != rend_ao_kernel_size; ++i) {
    const GeoVector randInCone = geo_vector_rand_in_cone3(rng, s->aoAngle);
    const f32       rand       = rng_sample_f32(rng);
    const f32       mag = math_lerp(0.1f, 1.0f, math_pow_f32(rand, s->aoRadiusPower)) * s->aoRadius;
    s->aoKernel[i]      = geo_vector_mul(randInCone, mag);
  }
}
