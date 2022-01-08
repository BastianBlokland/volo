#pragma once
#include "core_types.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

/**
 * Identifier for a timestamp record.
 */
typedef u32 RvkStopwatchRecord;

typedef struct sRvkStopwatch RvkStopwatch;

RvkStopwatch* rvk_stopwatch_create(RvkDevice*);
void          rvk_stopwatch_destroy(RvkStopwatch*);
bool          rvk_stopwatch_is_supported(const RvkStopwatch*);

/**
 * Reset all timestamps.
 * NOTE: Call this before marking new timestamps.
 */
void rvk_stopwatch_reset(RvkStopwatch*, VkCommandBuffer);

/**
 * Retrieve the result of a previously marked timestamp (in nanoseconds).
 * NOTE: Make sure the gpu work has finished before calling this.
 */
f64 rvk_stopwatch_nano(RvkStopwatch*, RvkStopwatchRecord);

/**
 * Mark a timestamp to be recorded.
 * Time will be taken after all previously recorded commands have finished executing.
 * Returns a record that can be used to retrieve the timestamp when rendering has finished
 */
RvkStopwatchRecord rvk_stopwatch_mark(RvkStopwatch*, VkCommandBuffer);
