#pragma once
#include "gap_window.h"
#include "rend_settings.h"
#include "rend_stats.h"

#include "types_internal.h"

#define rvk_canvas_max_passes 16

// Forward declare from 'geo_color.h'.
typedef union uGeoColor GeoColor;

// Internal forward declarations:
typedef struct sRvkAttachPool     RvkAttachPool;
typedef struct sRvkDevice         RvkDevice;
typedef struct sRvkImage          RvkImage;
typedef struct sRvkJob            RvkJob;
typedef struct sRvkPass           RvkPass;
typedef struct sRvkRepository     RvkRepository;
typedef struct sRvkSwapchainStats RvkSwapchainStats;

typedef struct sRvkCanvasStats {
  TimeDuration  waitForGpuDur; // Time the cpu was blocked waiting for the gpu.
  TimeDuration  gpuExecDur;
  u32           passCount;
  RendStatsPass passes[rvk_canvas_max_passes];
} RvkCanvasStats;

/**
 * Canvas for rendering onto a window.
 */
typedef struct sRvkCanvas RvkCanvas;

RvkCanvas* rvk_canvas_create(RvkDevice*, const GapWindowComp*);

void rvk_canvas_destroy(RvkCanvas*);

const RvkRepository* rvk_canvas_repository(const RvkCanvas*);
RvkAttachPool*       rvk_canvas_attach_pool(RvkCanvas*);
RvkJob*              rvk_canvas_job(RvkCanvas*);

/**
 * Query statistics about the previous submitted draw.
 */
void rvk_canvas_stats(const RvkCanvas*, RvkCanvasStats* out);

bool rvk_canvas_begin(RvkCanvas*, const RendSettingsComp*, RvkSize);

void rvk_canvas_pass_push(RvkCanvas*, RvkPass* pass);

void      rvk_canvas_swapchain_stats(const RvkCanvas*, RvkSwapchainStats* out);
RvkSize   rvk_canvas_swapchain_size(const RvkCanvas*);
RvkImage* rvk_canvas_swapchain_image(RvkCanvas*);

void rvk_canvas_end(RvkCanvas*);

/**
 * Wait for the previous frame to be rendered.
 */
bool rvk_canvas_wait_for_prev_present(const RvkCanvas*);
