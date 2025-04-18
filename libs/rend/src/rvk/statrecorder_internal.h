#pragma once
#include "core.h"

#include "forward_internal.h"

#include "vulkan_api.h"

/**
 * Tracked pipeline statistic.
 */
typedef enum eRvkStat {
  RvkStat_InputAssemblyVertices,
  RvkStat_InputAssemblyPrimitives,
  RvkStat_ShaderInvocationsVert,
  RvkStat_ShaderInvocationsFrag,

  RvkStat_Count,
} RvkStat;

/**
 * Identifier for a stat record.
 */
typedef u32 RvkStatRecord;

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
u64 rvk_statrecorder_query(const RvkStatRecorder*, RvkStatRecord, RvkStat);

RvkStatRecord rvk_statrecorder_start(RvkStatRecorder*, VkCommandBuffer);
void          rvk_statrecorder_stop(RvkStatRecorder*, RvkStatRecord, VkCommandBuffer);
