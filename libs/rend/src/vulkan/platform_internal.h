#pragma once
#include "gap_window.h"

typedef u16 RendVkCanvasId;

typedef struct sRendVkPlatform RendVkPlatform;

RendVkPlatform* rend_vk_platform_create();
void            rend_vk_platform_destroy(RendVkPlatform*);
RendVkCanvasId  rend_vk_platform_canvas_create(RendVkPlatform*, const GapWindowComp*);
void            rend_vk_platform_canvas_destroy(RendVkPlatform*, RendVkCanvasId);
void            rend_vk_platform_canvas_resize(RendVkPlatform*, RendVkCanvasId, GapVector size);
