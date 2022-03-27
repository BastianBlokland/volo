#pragma once
#include "gap_window.h"
#include "rend_settings.h"

#include "renderer_internal.h"
#include "swapchain_internal.h"
#include "types_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkPass   RvkPass;

typedef struct sRvkCanvas RvkCanvas;

RvkCanvas* rvk_canvas_create(RvkDevice*, const GapWindowComp*);
void       rvk_canvas_destroy(RvkCanvas*);

/**
 * Query statistics about the previous submitted draw.
 * NOTE: Will block until the previous draw has finished.
 */
RvkRenderStats rvk_canvas_render_stats(const RvkCanvas*);

/**
 * Query swapchain statistics.
 */
RvkSwapchainStats rvk_canvas_swapchain_stats(const RvkCanvas*);

bool     rvk_canvas_begin(RvkCanvas*, const RendSettingsComp*, RvkSize);
RvkPass* rvk_canvas_pass_forward(RvkCanvas*);
void     rvk_canvas_end(RvkCanvas*);
