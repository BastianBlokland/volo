#pragma once
#include "core_alloc.h"
#include "gap_window.h"

#include "device_internal.h"

typedef struct sRendVkCanvas RendVkCanvas;

RendVkCanvas* rend_vk_canvas_create(RendVkDevice* device, const GapWindowComp* window);
void          rend_vk_canvas_destroy(RendVkCanvas*);
void          rend_vk_canvas_resize(RendVkCanvas*, GapVector size);
