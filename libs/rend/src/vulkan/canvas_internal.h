#pragma once
#include "core_alloc.h"
#include "gap_vector.h"

typedef struct sRendVkCanvas RendVkCanvas;

RendVkCanvas* rend_vk_canvas_create(GapVector size);
void          rend_vk_canvas_destroy(RendVkCanvas*);
void          rend_vk_canvas_resize(RendVkCanvas*, GapVector size);
