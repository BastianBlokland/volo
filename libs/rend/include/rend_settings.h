#pragma once
#include "ecs_module.h"
#include "geo.h"

#define rend_ao_kernel_size 16

typedef enum eRendFlags {
  RendFlags_FrustumCulling       = 1 << 0,
  RendFlags_AmbientOcclusion     = 1 << 1,
  RendFlags_AmbientOcclusionBlur = 1 << 2,
  RendFlags_Shadows              = 1 << 3,
  RendFlags_Bloom                = 1 << 4,
  RendFlags_Distortion           = 1 << 5,
  RendFlags_Decals               = 1 << 6,
  RendFlags_VfxShadows           = 1 << 7,
  RendFlags_DebugWireframe       = 1 << 8,
  RendFlags_DebugCamera          = 1 << 9,
  RendFlags_DebugSkinning        = 1 << 10,
  RendFlags_DebugFog             = 1 << 11,
  RendFlags_DebugShadow          = 1 << 12,
  RendFlags_DebugDistortion      = 1 << 13,

  RendFlags_DebugOverlay = RendFlags_DebugFog | RendFlags_DebugShadow | RendFlags_DebugDistortion,
} RendFlags;

typedef enum eRendPresentMode {
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

typedef enum eRendAmbientMode {
  RendAmbientMode_Solid,
  RendAmbientMode_DiffuseIrradiance,
  RendAmbientMode_SpecularIrradiance,

  // Debug modes.
  RendAmbientMode_DebugStart,
  RendAmbientMode_DebugColor = RendAmbientMode_DebugStart,
  RendAmbientMode_DebugRoughness,
  RendAmbientMode_DebugMetalness,
  RendAmbientMode_DebugEmissive,
  RendAmbientMode_DebugNormal,
  RendAmbientMode_DebugDepth,
  RendAmbientMode_DebugTags,
  RendAmbientMode_DebugAmbientOcclusion,
  RendAmbientMode_DebugFresnel,
  RendAmbientMode_DebugDiffuseIrradiance,
  RendAmbientMode_DebugSpecularIrradiance,
} RendAmbientMode;

typedef enum eRendSkyMode {
  RendSkyMode_None,
  RendSkyMode_Gradient,
  RendSkyMode_CubeMap,
} RendSkyMode;

typedef enum eRendTonemapper {
  RendTonemapper_Linear,
  RendTonemapper_LinearSmooth,
  RendTonemapper_Reinhard,
  RendTonemapper_ReinhardJodie,
  RendTonemapper_Aces,
} RendTonemapper;

typedef enum {
  RendDebugViewer_Interpolate = 1 << 0, // Enable linear interpolation for textures in the viewer.
  RendDebugViewer_AlphaIgnore = 1 << 1, // Ignore the alpha when viewing textures in the viewer.
  RendDebugViewer_AlphaOnly   = 1 << 2, // Show only alpha when viewing textures in the viewer.
} RendDebugViewerFlags;

ecs_comp_extern_public(RendSettingsComp) {
  RendFlags            flags;
  RendPresentMode      presentMode;
  RendAmbientMode      ambientMode;
  RendSkyMode          skyMode;
  f32                  exposure;
  RendTonemapper       tonemapper;
  f32                  resolutionScale;
  u16                  shadowResolution, fogResolution;
  f32                  aoAngle, aoRadius, aoRadiusPower, aoPower, aoResolutionScale;
  GeoVector*           aoKernel; // GeoVector[rend_ao_kernel_size];
  u32                  fogBlurSteps;
  f32                  fogBlurScale;
  f32                  bloomIntensity;
  u32                  bloomSteps;
  f32                  bloomRadius;
  f32                  distortionResolutionScale;
  EcsEntityId          debugViewerResource; // Resource entity to visualize for debug purposes.
  f32                  debugViewerLod;      // Level-of-detail to use for the debug-viewer.
  RendDebugViewerFlags debugViewerFlags;    // Flags to use for the debug-viewer.
};

typedef enum eRendGlobalFlags {
  RendGlobalFlags_Validation       = 1 << 0,
  RendGlobalFlags_Verbose          = 1 << 1,
  RendGlobalFlags_DebugGpu         = 1 << 2,
  RendGlobalFlags_DebugLight       = 1 << 3,
  RendGlobalFlags_DebugLightFreeze = 1 << 4,
  RendGlobalFlags_Fog              = 1 << 5,
} RendGlobalFlags;

ecs_comp_extern_public(RendSettingsGlobalComp) {
  RendGlobalFlags flags;
  u16             limiterFreq;
  f32             shadowFilterSize; // In world space.
  f32             fogDilation;
};

RendSettingsGlobalComp* rend_settings_global_init(EcsWorld*);
RendSettingsComp*       rend_settings_window_init(EcsWorld*, EcsEntityId window);

void rend_settings_to_default(RendSettingsComp*);
void rend_settings_global_to_default(RendSettingsGlobalComp*);

void rend_settings_generate_ao_kernel(RendSettingsComp*);
