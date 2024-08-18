#pragma once
#include "core_time.h"
#include "rend_pass.h"

#include "vulkan_internal.h"

// Forward declare from 'geo_color.h'.
typedef union uGeoColor GeoColor;

// Internal forward declarations:
typedef enum eRvkImagePhase    RvkImagePhase;
typedef struct sRvkCanvasStats RvkCanvasStats;
typedef struct sRvkDevice      RvkDevice;
typedef struct sRvkImage       RvkImage;
typedef struct sRvkPass        RvkPass;
typedef struct sRvkPassConfig  RvkPassConfig;

typedef struct sRvkJob RvkJob;

RvkJob* rvk_job_create(
    RvkDevice*,
    VkFormat             swapchainFormat,
    u32                  jobId,
    const RvkPassConfig* passConfig /* RvkPassConfig[RendPass_Count] */);
void           rvk_job_destroy(RvkJob*);
bool           rvk_job_is_done(const RvkJob*);
void           rvk_job_wait_for_done(const RvkJob*);
RvkCanvasStats rvk_job_stats(const RvkJob*);

void rvk_job_begin(RvkJob*);

RvkPass* rvk_job_pass(RvkJob*, RendPass);

void rvk_job_img_clear_color(RvkJob*, RvkImage*, GeoColor);
void rvk_job_img_clear_depth(RvkJob*, RvkImage*, f32 depth);
void rvk_job_img_copy(RvkJob*, RvkImage* src, RvkImage* dst);
void rvk_job_img_blit(RvkJob*, RvkImage* src, RvkImage* dst);
void rvk_job_img_transition(RvkJob*, RvkImage* img, RvkImagePhase phase);

/**
 * Full barrier; will flush and invalidate all caches and stall everything. Only for debugging.
 */
void rvk_job_barrier_full(RvkJob*);

void rvk_job_end(
    RvkJob*,
    VkSemaphore        waitForDeps,
    VkSemaphore        waitForTarget,
    const VkSemaphore* signals,
    u32                signalCount);
