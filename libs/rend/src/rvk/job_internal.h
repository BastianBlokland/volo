#pragma once
#include "core_time.h"
#include "rend_pass.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef enum eRvkImagePhase    RvkImagePhase;
typedef enum eRvkPassFlags     RvkPassFlags;
typedef struct sRvkCanvasStats RvkCanvasStats;
typedef struct sRvkDevice      RvkDevice;
typedef struct sRvkImage       RvkImage;
typedef struct sRvkPass        RvkPass;

typedef struct sRvkJob RvkJob;

RvkJob* rvk_job_create(
    RvkDevice*,
    VkFormat            swapchainFormat,
    u32                 jobId,
    const RvkPassFlags* passConfig /* [ RendPass_Count ] */);
void           rvk_job_destroy(RvkJob*);
void           rvk_job_wait_for_done(const RvkJob*);
RvkCanvasStats rvk_job_stats(const RvkJob*);

void rvk_job_begin(RvkJob*);

RvkPass* rvk_job_pass(RvkJob*, RendPass);
void     rvk_job_copy(RvkJob*, RvkImage* src, RvkImage* dst);
void     rvk_job_blit(RvkJob*, RvkImage* src, RvkImage* dst);
void     rvk_job_transition(RvkJob*, RvkImage* img, RvkImagePhase phase);

void rvk_job_end(
    RvkJob*,
    VkSemaphore        waitForDeps,
    VkSemaphore        waitForTarget,
    const VkSemaphore* signals,
    u32                signalCount);
