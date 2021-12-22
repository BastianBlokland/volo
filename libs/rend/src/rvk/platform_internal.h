#pragma once
#include "gap_window.h"

typedef enum {
  RvkWellKnownId_MissingTexture,

  RvkWellKnownId_Count,
} RvkWellKnownId;

// Internal forward declarations:
typedef struct sRvkCanvas  RvkCanvas;
typedef struct sRvkDevice  RvkDevice;
typedef struct sRvkTexture RvkTexture;

typedef struct sRvkPlatform RvkPlatform;

RvkPlatform* rvk_platform_create();
void         rvk_platform_destroy(RvkPlatform*);
RvkDevice*   rvk_platform_device(const RvkPlatform*);
void         rvk_platform_update(RvkPlatform*);
void         rvk_platform_wait_idle(const RvkPlatform*);
RvkCanvas*   rvk_platform_canvas_create(RvkPlatform*, const GapWindowComp*);

void        rvk_platform_texture_set(RvkPlatform*, RvkWellKnownId, RvkTexture*);
RvkTexture* rvk_platform_texture_get(const RvkPlatform*, RvkWellKnownId);
