#pragma once
#include "core/time.h"
#include "geo/forward.h"

#include "forward.h"
#include "uniform.h"
#include "vulkan_api.h"

#define rvk_job_copy_stats_max 8

typedef enum eRvkJobPhase {
  RvkJobPhase_Main,
  RvkJobPhase_Output, // Work that can only be done when the output is available.

  RvkJobPhase_Count,
  RvkJobPhase_First = 0,
  RvkJobPhase_Last  = RvkJobPhase_Count - 1,
} RvkJobPhase;

typedef struct {
  TimeSteady gpuTimeBegin, gpuTimeEnd; // Requires 'RvkDeviceFlags_RecordStats'.
} RvkJobCopyStats;

typedef struct {
  TimeDuration cpuWaitDur;               // Time the cpu was blocked waiting for the gpu.
  TimeSteady   gpuTimeBegin, gpuTimeEnd; // Requires 'RvkDeviceFlags_RecordStats'.
  TimeSteady   gpuWaitBegin, gpuWaitEnd; // Requires 'RvkDeviceFlags_RecordStats'.

  u32             copyCount;
  RvkJobCopyStats copyStats[rvk_job_copy_stats_max];
} RvkJobStats;

typedef struct sRvkJob RvkJob;

RvkJob* rvk_job_create(RvkDevice*, u32 jobId);
void    rvk_job_destroy(RvkJob*);
bool    rvk_job_is_done(const RvkJob*);
void    rvk_job_wait_for_done(const RvkJob*);
bool    rvk_job_calibrated_timestamps(const RvkJob*);
void    rvk_job_stats(const RvkJob*, RvkJobStats* out);

void rvk_job_begin(RvkJob*, RvkJobPhase firstPhase);

RvkJobPhase rvk_job_phase(const RvkJob*);
void        rvk_job_advance(RvkJob*);

RvkUniformPool*  rvk_job_uniform_pool(RvkJob*);
RvkStopwatch*    rvk_job_stopwatch(RvkJob*);    // NOTE: Potentially null depending on device setup.
RvkStatRecorder* rvk_job_statrecorder(RvkJob*); // NOTE: Potentially null depending on device setup.
VkCommandBuffer  rvk_job_cmdbuffer(RvkJob*);

Mem              rvk_job_uniform_map(RvkJob*, RvkUniformHandle);
RvkUniformHandle rvk_job_uniform_push(RvkJob*, usize size);
RvkUniformHandle rvk_job_uniform_push_next(RvkJob*, RvkUniformHandle head, usize size);

void rvk_job_img_clear_color(RvkJob*, RvkImage*, GeoColor);
void rvk_job_img_clear_depth(RvkJob*, RvkImage*, f32 depth);
void rvk_job_img_copy(RvkJob*, RvkImage* src, RvkImage* dst);
void rvk_job_img_copy_batch(RvkJob*, RvkImage* srcImages[], RvkImage* dstImages[], u32 count);

void rvk_job_img_blit(RvkJob*, RvkImage* src, RvkImage* dst);
void rvk_job_img_transition(RvkJob*, RvkImage* img, RvkImagePhase phase);

/**
 * Full barrier; will flush and invalidate all caches and stall everything. Only for debugging.
 */
void rvk_job_barrier_full(RvkJob*);

void rvk_job_end(RvkJob*, VkSemaphore waitForTarget, const VkSemaphore signals[], u32 signalCount);
