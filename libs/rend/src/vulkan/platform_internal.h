#pragma once
#include "gap_window.h"
#include "rend_color.h"
#include "rend_size.h"

typedef u16 RendVkCanvasId;

typedef struct sRendVkPlatform RendVkPlatform;

RendVkPlatform* rend_vk_platform_create();
void            rend_vk_platform_destroy(RendVkPlatform*);
RendVkCanvasId  rend_vk_platform_canvas_create(RendVkPlatform*, const GapWindowComp*);
void            rend_vk_platform_canvas_destroy(RendVkPlatform*, RendVkCanvasId);

bool rend_vk_platform_draw_begin(RendVkPlatform*, RendVkCanvasId, RendSize, RendColor clearColor);
void rend_vk_platform_draw_end(RendVkPlatform*, RendVkCanvasId);
