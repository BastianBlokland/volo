#pragma once
#include "gap_window.h"
#include "rend_settings.h"
#include "rend_stats.h"

#include "types_internal.h"

#define rvk_canvas_max_passes 16

// Forward declare from 'geo_color.h'.
typedef union uGeoColor GeoColor;

// Internal forward declarations:
typedef struct sRvkDevice         RvkDevice;
typedef struct sRvkImage          RvkImage;
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

RvkCanvas* rvk_canvas_create(RvkDevice*, const GapWindowComp*, RvkPass* passes[], u32 passCount);

void rvk_canvas_destroy(RvkCanvas*);

RvkRepository* rvk_canvas_repository(RvkCanvas*);

/**
 * Query statistics about the previous submitted draw.
 */
void rvk_canvas_stats(const RvkCanvas*, RvkCanvasStats* out);
u16  rvk_canvas_attach_count(const RvkCanvas*);
u64  rvk_canvas_attach_memory(const RvkCanvas*);

/**
 * Query swapchain statistics.
 */
void rvk_canvas_swapchain_stats(const RvkCanvas*, RvkSwapchainStats* out);

bool rvk_canvas_begin(RvkCanvas*, const RendSettingsComp*, RvkSize);

RvkImage* rvk_canvas_swapchain_image(RvkCanvas*);

RvkImage* rvk_canvas_attach_acquire_color(RvkCanvas*, RvkPass*, const u32 i, RvkSize);
RvkImage* rvk_canvas_attach_acquire_depth(RvkCanvas*, RvkPass*, RvkSize);
RvkImage* rvk_canvas_attach_acquire_copy(RvkCanvas*, RvkImage*);
RvkImage* rvk_canvas_attach_acquire_copy_uninit(RvkCanvas*, RvkImage*);
void      rvk_canvas_attach_release(RvkCanvas*, RvkImage*);

void rvk_canvas_img_clear_color(RvkCanvas*, RvkImage*, GeoColor);
void rvk_canvas_img_clear_depth(RvkCanvas*, RvkImage*, f32 depth);
void rvk_canvas_img_copy(RvkCanvas*, RvkImage* src, RvkImage* dst);
void rvk_canvas_img_blit(RvkCanvas*, RvkImage* src, RvkImage* dst);

/**
 * Full barrier; will flush and invalidate all caches and stall everything. Only for debugging.
 */
void rvk_canvas_barrier_full(const RvkCanvas*);

void rvk_canvas_end(RvkCanvas*);

/**
 * Wait for the previous frame to be rendered.
 */
bool rvk_canvas_wait_for_prev_present(const RvkCanvas*);
