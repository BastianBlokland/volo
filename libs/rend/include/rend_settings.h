#pragma once
#include "ecs_module.h"

typedef enum {
  RendFlags_FrustumCulling = 1 << 0,
  RendFlags_Wireframe      = 1 << 1,
  RendFlags_DebugSkinning  = 1 << 2,
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
  RendShadeDebug_None,
  RendShadeDebug_Color,
  RendShadeDebug_Roughness,
  RendShadeDebug_Normal,
  RendShadeDebug_Depth,
  RendShadeDebug_Tags,
} RendShadeDebug;

ecs_comp_extern_public(RendSettingsComp) {
  RendFlags       flags;
  RendPresentMode presentMode;
  RendShadeDebug  shadeDebug;
  f32             resolutionScale;
};

typedef enum {
  RendGlobalFlags_Validation = 1 << 0,
  RendGlobalFlags_Verbose    = 1 << 1,
  RendGlobalFlags_DebugGpu   = 1 << 2,
  RendGlobalFlags_DebugLight = 1 << 3,
} RendGlobalFlags;

ecs_comp_extern_public(RendGlobalSettingsComp) {
  RendGlobalFlags flags;
  u16             limiterFreq;
};

void rend_settings_to_default(RendSettingsComp*);
void rend_global_settings_to_default(RendGlobalSettingsComp*);
