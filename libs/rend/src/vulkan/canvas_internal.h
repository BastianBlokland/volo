#pragma once
#include "core_alloc.h"

#include "device_internal.h"
#include "swapchain_internal.h"

typedef struct {
  RendVkDevice*    device;
  RendVkSwapchain* swapchain;
} RendVkCanvas;

RendVkCanvas* rend_vk_canvas_create(RendVkDevice*, const GapWindowComp*);
void          rend_vk_canvas_destroy(RendVkCanvas*);
void          rend_vk_canvas_resize(RendVkCanvas*, GapVector size);
