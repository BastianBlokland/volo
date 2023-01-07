#pragma once
#include "gap_window.h"
#include "rend_settings.h"

#include "types_internal.h"

// Internal forward declarations:
typedef enum eRvkRenderPass       RvkRenderPass;
typedef struct sRvkAttachPool     RvkAttachPool;
typedef struct sRvkDevice         RvkDevice;
typedef struct sRvkImage          RvkImage;
typedef struct sRvkPass           RvkPass;
typedef struct sRvkRenderStats    RvkRenderStats;
typedef struct sRvkRepository     RvkRepository;
typedef struct sRvkSwapchainStats RvkSwapchainStats;

/**
 * Canvas for rendering onto a window.
 */
typedef struct sRvkCanvas RvkCanvas;

RvkCanvas* rvk_canvas_create(RvkDevice*, const GapWindowComp*);
void       rvk_canvas_destroy(RvkCanvas*);

RvkAttachPool* rvk_canvas_attach_pool(RvkCanvas*);
RvkRepository* rvk_canvas_repository(RvkCanvas*);

/**
 * Query statistics about the previous submitted draw.
 * NOTE: Will block until the previous draw has finished.
 */
RvkRenderStats rvk_canvas_render_stats(const RvkCanvas*);

/**
 * Query swapchain statistics.
 */
RvkSwapchainStats rvk_canvas_swapchain_stats(const RvkCanvas*);

bool      rvk_canvas_begin(RvkCanvas*, const RendSettingsComp*, RvkSize);
RvkPass*  rvk_canvas_pass(RvkCanvas*, RvkRenderPass);
RvkImage* rvk_canvas_output(RvkCanvas*);
void      rvk_canvas_copy(RvkCanvas*, RvkImage* src, RvkImage* dst);
void      rvk_canvas_blit(RvkCanvas*, RvkImage* src, RvkImage* dst);
void      rvk_canvas_end(RvkCanvas*);

/**
 * Wait for the previously rendered image to be presented to the user.
 * NOTE: Is a no-op if the device and/or driver does not support tracking presentations.
 */
void rvk_canvas_wait_for_prev_present(const RvkCanvas*);
