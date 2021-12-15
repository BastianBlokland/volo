#pragma once
#include "gap_window.h"
#include "rend_color.h"
#include "rend_size.h"

// Internal forward declarations:
typedef struct sRvkDevice  RvkDevice;
typedef struct sRvkGraphic RvkGraphic;

typedef u16 RvkCanvasId;

typedef struct sRvkPlatform RvkPlatform;

RvkPlatform* rvk_platform_create();
void         rvk_platform_destroy(RvkPlatform*);
RvkDevice*   rvk_platform_device(const RvkPlatform*);
RvkCanvasId  rvk_platform_canvas_create(RvkPlatform*, const GapWindowComp*);
void         rvk_platform_canvas_destroy(RvkPlatform*, RvkCanvasId);

bool rvk_platform_draw_begin(RvkPlatform*, RvkCanvasId, RendSize, RendColor clearColor);
void rvk_platform_draw_inst(RvkPlatform*, RvkCanvasId, RvkGraphic*);
void rvk_platform_draw_end(RvkPlatform*, RvkCanvasId);
