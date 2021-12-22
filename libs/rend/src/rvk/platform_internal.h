#pragma once
#include "gap_window.h"

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
