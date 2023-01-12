#pragma once
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"

#define rend_ao_kernel_size 16

typedef enum {
  RendFlags_FrustumCulling       = 1 << 0,
  RendFlags_AmbientOcclusion     = 1 << 1,
  RendFlags_AmbientOcclusionBlur = 1 << 2,
  RendFlags_Wireframe            = 1 << 3,
  RendFlags_DebugCamera          = 1 << 4,
  RendFlags_DebugSkinning        = 1 << 5,
  RendFlags_DebugShadow          = 1 << 6,
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
  RendAmbientMode_Normal,
  RendAmbientMode_DebugColor,
  RendAmbientMode_DebugRoughness,
  RendAmbientMode_DebugNormal,
  RendAmbientMode_DebugDepth,
  RendAmbientMode_DebugTags,
  RendAmbientMode_DebugAmbientOcclusion,
} RendAmbientMode;

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
  f32             exposure;
  RendTonemapper  tonemapper;
  f32             resolutionScale;
  u16             shadowResolution;
  f32             aoAngle, aoRadius, aoRadiusPower, aoPower, aoResolutionScale;
  GeoVector*      aoKernel; // GeoVector[rend_ao_kernel_size];
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
};

void rend_settings_to_default(RendSettingsComp*);
void rend_settings_global_to_default(RendSettingsGlobalComp*);

void rend_settings_generate_ao_kernel(RendSettingsComp*);
