#pragma once
#include "ecs_module.h"

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

ecs_comp_extern_public(RendSettingsComp) { RendPresentMode presentMode; };
