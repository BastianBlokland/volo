#pragma once
#include "core_time.h"

#include "uniform_internal.h"

// Forward declare from 'geo_color.h'.
typedef union uGeoColor GeoColor;

// Internal forward declarations:
typedef enum eRvkImagePhase     RvkImagePhase;
typedef struct sRvkDevice       RvkDevice;
typedef struct sRvkImage        RvkImage;
typedef struct sRvkStatRecorder RvkStatRecorder;
typedef struct sRvkStopwatch    RvkStopwatch;
typedef struct sRvkUniformPool  RvkUniformPool;

typedef struct {
  TimeDuration waitForGpuDur; // Time the cpu was blocked waiting for the gpu.
  TimeDuration gpuExecDur;
} RvkJobStats;

typedef struct sRvkJob RvkJob;

RvkJob* rvk_job_create(RvkDevice*, u32 jobId);
void    rvk_job_destroy(RvkJob*);
bool    rvk_job_is_done(const RvkJob*);
void    rvk_job_wait_for_done(const RvkJob*);
void    rvk_job_stats(const RvkJob*, RvkJobStats* out);

void rvk_job_begin(RvkJob*);

RvkUniformPool*  rvk_job_uniform_pool(RvkJob*);
RvkStopwatch*    rvk_job_stopwatch(RvkJob*);
RvkStatRecorder* rvk_job_statrecorder(RvkJob*);
VkCommandBuffer  rvk_job_cmdbuffer(RvkJob*);

Mem              rvk_job_uniform_map(RvkJob*, RvkUniformHandle);
RvkUniformHandle rvk_job_uniform_push(RvkJob*, usize size);
RvkUniformHandle rvk_job_uniform_push_next(RvkJob*, RvkUniformHandle head, usize size);

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
