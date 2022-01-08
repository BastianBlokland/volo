#pragma once
#include "core_types.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef enum {
  RvkStat_InputAssemblyVertices,
  RvkStat_InputAssemblyPrimitives,
  RvkStat_ShaderInvocationsVert,
  RvkStat_ShaderInvocationsFrag,

  RvkStat_Count,
} RvkStat;

typedef struct sRvkStatRecorder RvkStatRecorder;

RvkStatRecorder* rvk_statrecorder_create(RvkDevice*);
void             rvk_statrecorder_destroy(RvkStatRecorder*);
bool             rvk_statrecorder_is_supported(const RvkStatRecorder*);

/**
 * Reset all statistics.
 * NOTE: Call this before starting a new capture.
 */
void rvk_statrecorder_reset(RvkStatRecorder*, VkCommandBuffer);

/**
 * Retrieve the result statistic of the last capture.
 * NOTE: Make sure the gpu work has finished before calling this.
 */
u64 rvk_statrecorder_query(const RvkStatRecorder*, RvkStat);

void rvk_statrecorder_start(RvkStatRecorder*, VkCommandBuffer);
void rvk_statrecorder_stop(RvkStatRecorder*, VkCommandBuffer);
