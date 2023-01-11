#pragma once
#include "gap_window.h"
#include "rend_pass.h"
#include "rend_settings.h"
#include "rend_stats.h"

#include "types_internal.h"

// Internal forward declarations:
typedef enum eRvkPassFlags        RvkPassFlags;
typedef struct sRvkDevice         RvkDevice;
typedef struct sRvkImage          RvkImage;
typedef struct sRvkPass           RvkPass;
typedef struct sRvkRenderStats    RvkRenderStats;
typedef struct sRvkRepository     RvkRepository;
typedef struct sRvkSwapchainStats RvkSwapchainStats;

typedef struct sRvkCanvasStats {
  TimeDuration renderDur, waitForRenderDur;
  RendStatPass passes[RendPass_Count];
} RvkCanvasStats;

/**
 * Canvas for rendering onto a window.
 */
typedef struct sRvkCanvas RvkCanvas;

RvkCanvas* rvk_canvas_create(
    RvkDevice*, const GapWindowComp*, const RvkPassFlags* passConfig /* [ RendPass_Count ] */);
void rvk_canvas_destroy(RvkCanvas*);

RvkRepository* rvk_canvas_repository(RvkCanvas*);

/**
 * Query statistics about the previous submitted draw.
 * NOTE: Will block until the previous draw has finished.
 */
RvkCanvasStats rvk_canvas_stats(const RvkCanvas*);
u16            rvk_canvas_attach_count(const RvkCanvas*);
u64            rvk_canvas_attach_memory(const RvkCanvas*);

/**
 * Query swapchain statistics.
 */
RvkSwapchainStats rvk_canvas_swapchain_stats(const RvkCanvas*);

bool rvk_canvas_begin(RvkCanvas*, const RendSettingsComp*, RvkSize);

RvkPass*  rvk_canvas_pass(RvkCanvas*, RendPass);
RvkImage* rvk_canvas_swapchain_image(RvkCanvas*);

RvkImage* rvk_canvas_attach_acquire_color(RvkCanvas*, RvkPass*, const u32 i);
RvkImage* rvk_canvas_attach_acquire_depth(RvkCanvas*, RvkPass*);
void      rvk_canvas_attach_release(RvkCanvas*, RvkImage*);

void rvk_canvas_copy(RvkCanvas*, RvkImage* src, RvkImage* dst);
void rvk_canvas_blit(RvkCanvas*, RvkImage* src, RvkImage* dst);

void rvk_canvas_end(RvkCanvas*);

/**
 * Wait for the previously rendered image to be presented to the user.
 * NOTE: Is a no-op if the device and/or driver does not support tracking presentations.
 */
void rvk_canvas_wait_for_prev_present(const RvkCanvas*);
