#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"

#define rend_ao_kernel_size 16

typedef enum {
  RendFlags_FrustumCulling       = 1 << 0,
  RendFlags_AmbientOcclusion     = 1 << 1,
  RendFlags_AmbientOcclusionBlur = 1 << 2,
  RendFlags_Bloom                = 1 << 3,
  RendFlags_Distortion           = 1 << 4,
  RendFlags_Decals               = 1 << 5,
  RendFlags_ParticleShadows      = 1 << 6,
  RendFlags_DebugWireframe       = 1 << 7,
  RendFlags_DebugCamera          = 1 << 8,
  RendFlags_DebugSkinning        = 1 << 9,
  RendFlags_DebugFog             = 1 << 10,
  RendFlags_DebugShadow          = 1 << 11,
  RendFlags_DebugDistortion      = 1 << 12,

  RendFlags_DebugOverlay = RendFlags_DebugFog | RendFlags_DebugShadow | RendFlags_DebugDistortion,
} RendFlags;

typedef enum {
  /**
   * Don't wait for a vblank but immediately output the new image.
   * NOTE: This mode may result in visible tearing.
   */
  RendPresentMode_Immediate,

  /**
   * Wait for the next vblank to output the new image.
   */
  RendPresentMode_VSync,

  /**
   * Wait for the next vblank if the application is early, if the application is late then
   * immediately output the new image.
   * NOTE: This mode may result in visible tearing when the application is late for the vblank.
   */
  RendPresentMode_VSyncRelaxed,

  /**
   * Wait for the next vblank to output a new image, but does not block acquiring a next image.
   * If the application finishes another image before the vblank then it will replace the currently
   * waiting image.
   */
  RendPresentMode_Mailbox,

} RendPresentMode;

typedef enum {
  RendAmbientMode_Solid,
  RendAmbientMode_DiffuseIrradiance,
  RendAmbientMode_SpecularIrradiance,

  // Debug modes.
  RendAmbientMode_DebugStart,
  RendAmbientMode_DebugColor = RendAmbientMode_DebugStart,
  RendAmbientMode_DebugRoughness,
  RendAmbientMode_DebugEmissive,
  RendAmbientMode_DebugNormal,
  RendAmbientMode_DebugDepth,
  RendAmbientMode_DebugTags,
  RendAmbientMode_DebugAmbientOcclusion,
  RendAmbientMode_DebugFresnel,
  RendAmbientMode_DebugDiffuseIrradiance,
  RendAmbientMode_DebugSpecularIrradiance,
} RendAmbientMode;

typedef enum {
  RendSkyMode_None,
  RendSkyMode_Gradient,
  RendSkyMode_CubeMap,
} RendSkyMode;

typedef enum {
  RendTonemapper_Linear,
  RendTonemapper_LinearSmooth,
  RendTonemapper_Reinhard,
  RendTonemapper_ReinhardJodie,
  RendTonemapper_Aces,
} RendTonemapper;

ecs_comp_extern_public(RendSettingsComp) {
  RendFlags       flags;
  RendPresentMode presentMode;
  RendAmbientMode ambientMode;
  RendSkyMode     skyMode;
  f32             exposure;
  RendTonemapper  tonemapper;
  f32             resolutionScale;
  u16             shadowResolution, fogResolution;
  f32             aoAngle, aoRadius, aoRadiusPower, aoPower, aoResolutionScale;
  GeoVector*      aoKernel; // GeoVector[rend_ao_kernel_size];
  f32             bloomIntensity;
  u32             bloomSteps;
  f32             bloomRadius;
  f32             distortionResolutionScale;
  EcsEntityId     debugViewerResource; // Resource entity to visualize for debug purposes.
};

typedef enum {
  RendGlobalFlags_SunShadows  = 1 << 0,
  RendGlobalFlags_SunCoverage = 1 << 1,
  RendGlobalFlags_Validation  = 1 << 2,
  RendGlobalFlags_Verbose     = 1 << 3,
  RendGlobalFlags_DebugGpu    = 1 << 4,
  RendGlobalFlags_DebugLight  = 1 << 5,
} RendGlobalFlags;

ecs_comp_extern_public(RendSettingsGlobalComp) {
  RendGlobalFlags flags;
  u16             limiterFreq;

  f32      lightAmbient;
  GeoColor lightSunRadiance;
  GeoQuat  lightSunRotation;
  f32      shadowFilterSize; // In world space.
};

RendSettingsGlobalComp* rend_settings_global_init(EcsWorld*);
RendSettingsComp*       rend_settings_window_init(EcsWorld*, EcsEntityId window);

void rend_settings_to_default(RendSettingsComp*);
void rend_settings_global_to_default(RendSettingsGlobalComp*);

void rend_settings_generate_ao_kernel(RendSettingsComp*);
