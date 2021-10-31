#pragma once
#include "core_alloc.h"

#include "device_internal.h"
#include "renderer_internal.h"
#include "swapchain_internal.h"
#include "technique_internal.h"

typedef struct {
  RendVkDevice*    device;
  RendVkSwapchain* swapchain;
  RendVkTechnique* technique;
  RendVkRenderer*  renderers[2];
  u32              rendererIdx;
  RendSwapchainIdx curSwapchainIdx;
} RendVkCanvas;

RendVkCanvas* rend_vk_canvas_create(RendVkDevice*, const GapWindowComp*);
void          rend_vk_canvas_destroy(RendVkCanvas*);
bool          rend_vk_canvas_draw_begin(RendVkCanvas*, GapVector size);
void          rend_vk_canvas_draw_end(RendVkCanvas*);
