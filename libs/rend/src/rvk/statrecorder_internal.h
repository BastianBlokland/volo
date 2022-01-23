#pragma once
#include "core_types.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

/**
 * Tracked statistic.
 */
typedef enum {
  /**
   * Stats that are automatically tracked for you when using 'start()' / 'stop()'.
   */
  RvkStat_InputAssemblyVertices,
  RvkStat_InputAssemblyPrimitives,
  RvkStat_ShaderInvocationsVert,
  RvkStat_ShaderInvocationsFrag,

  RvkStatMeta_CountAuto,
  /**
   * Stats that can be manually reported using the 'report()' api.
   */
  RvkStat_Draws = RvkStatMeta_CountAuto,

  RvkStatMeta_CountTotal,
  RvkStatMeta_CountManual = RvkStatMeta_CountTotal - RvkStatMeta_CountTotal,
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

/**
 * Report values to be added to a manually tracked stat.
 * NOTE: The given count is added to the current total.
 * Pre-condition: The given RvkStat is a 'manual' stat and not an automatic one.
 */
void rvk_statrecorder_report(RvkStatRecorder*, RvkStat, u32 count);

void rvk_statrecorder_start(RvkStatRecorder*, VkCommandBuffer);
void rvk_statrecorder_stop(RvkStatRecorder*, VkCommandBuffer);
