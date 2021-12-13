#pragma once
#include "core_alloc.h"
#include "rend_color.h"
#include "rend_size.h"

#include "renderer_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice    RvkDevice;
typedef struct sRvkSwapchain RvkSwapchain;
typedef struct sRvkTechnique RvkTechnique;

typedef struct sRvkCanvas {
  RvkDevice*      device;
  RvkSwapchain*   swapchain;
  RvkTechnique*   technique;
  RvkRenderer*    renderers[2];
  u32             rendererIdx;
  RvkSwapchainIdx swapchainIdx;
} RvkCanvas;

RvkCanvas* rvk_canvas_create(RvkDevice*, const GapWindowComp*);
void       rvk_canvas_destroy(RvkCanvas*);
bool       rvk_canvas_draw_begin(RvkCanvas*, RendSize, RendColor clearColor);
void       rvk_canvas_draw_end(RvkCanvas*);
