#pragma once
#include "gap_window.h"
#include "rend_settings.h"

#include "renderer_internal.h"
#include "swapchain_internal.h"
#include "types_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice     RvkDevice;
typedef struct sRvkPass       RvkPass;
typedef struct sRvkRepository RvkRepository;

typedef struct sRvkCanvas RvkCanvas;

RvkCanvas* rvk_canvas_create(RvkDevice*, const GapWindowComp*);
void       rvk_canvas_destroy(RvkCanvas*);

/**
 * Get a pointer to the device's resource repository.
 */
RvkRepository* rvk_canvas_repository(RvkCanvas* canvas);

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
RvkPass* rvk_canvas_pass(RvkCanvas*, RvkRenderPass);
void     rvk_canvas_end(RvkCanvas*);

/**
 * Wait for the previously rendered image to be presented to the user.
 * NOTE: Is a no-op if the device and/or driver does not support tracking presentations.
 */
void rvk_canvas_wait_for_prev_present(const RvkCanvas*);
