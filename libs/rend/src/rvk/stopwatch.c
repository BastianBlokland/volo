#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "device_internal.h"
#include "stopwatch_internal.h"

#define rvk_stopwatch_timestamps_max 64

typedef enum {
  RvkStopwatch_Supported  = 1 << 0,
  RvkStopwatch_HasResults = 1 << 1,
} RvkStopwatchFlags;

struct sRvkStopwatch {
  RvkDevice*        dev;
  ThreadMutex       retrieveResultsMutex;
  VkQueryPool       vkQueryPool;
  u32               counter;
  RvkStopwatchFlags flags;
  u64               results[rvk_stopwatch_timestamps_max];
};

static VkQueryPool rvk_querypool_create(RvkDevice* dev) {
  const VkQueryPoolCreateInfo createInfo = {
      .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType  = VK_QUERY_TYPE_TIMESTAMP,
      .queryCount = rvk_stopwatch_timestamps_max,
  };
  VkQueryPool result;
  rvk_call(vkCreateQueryPool, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static void rvk_stopwatch_retrieve_results(RvkStopwatch* sw) {
  thread_mutex_lock(sw->retrieveResultsMutex);
  if (!(sw->flags & RvkStopwatch_HasResults) && sw->counter) {
    rvk_call(
        vkGetQueryPoolResults,
        sw->dev->vkDev,
        sw->vkQueryPool,
        0,
        sw->counter,
        sizeof(sw->results),
        sw->results,
        sizeof(u64),
        VK_QUERY_RESULT_64_BIT);
    sw->flags |= RvkStopwatch_HasResults;
  }
  thread_mutex_unlock(sw->retrieveResultsMutex);
}

RvkStopwatch* rvk_stopwatch_create(RvkDevice* dev) {
  RvkStopwatch* sw = alloc_alloc_t(g_alloc_heap, RvkStopwatch);

  *sw = (RvkStopwatch){
      .dev                  = dev,
      .retrieveResultsMutex = thread_mutex_create(g_alloc_heap),
  };

  if (dev->vkProperties.limits.timestampComputeAndGraphics) {
    sw->vkQueryPool = rvk_querypool_create(dev);
    sw->flags |= RvkStopwatch_Supported;
  } else {
    log_w("Vulkan device does not support timestamps");
  }
  return sw;
}

void rvk_stopwatch_destroy(RvkStopwatch* sw) {
  if (sw->flags & RvkStopwatch_Supported) {
    vkDestroyQueryPool(sw->dev->vkDev, sw->vkQueryPool, &sw->dev->vkAlloc);
  }
  thread_mutex_destroy(sw->retrieveResultsMutex);
  alloc_free_t(g_alloc_heap, sw);
}

bool rvk_stopwatch_is_supported(const RvkStopwatch* sw) {
  return (sw->flags & RvkStopwatch_Supported) != 0;
}

void rvk_stopwatch_reset(RvkStopwatch* sw, VkCommandBuffer vkCmdBuf) {
  if (LIKELY(sw->flags & RvkStopwatch_Supported)) {
    vkCmdResetQueryPool(vkCmdBuf, sw->vkQueryPool, 0, rvk_stopwatch_timestamps_max);
  }
  sw->counter = 0;
  sw->flags &= ~RvkStopwatch_HasResults;
}

u64 rvk_stopwatch_query(const RvkStopwatch* sw, const RvkStopwatchRecord record) {
  diag_assert(record < rvk_stopwatch_timestamps_max);
  if (UNLIKELY(!(sw->flags & RvkStopwatch_Supported))) {
    return 0;
  }

  if (!(sw->flags & RvkStopwatch_HasResults)) {
    RvkStopwatch* swMutable = (RvkStopwatch*)sw;
    rvk_stopwatch_retrieve_results(swMutable);
  }

  return (u64)(sw->results[record] * (f64)sw->dev->vkProperties.limits.timestampPeriod);
}

RvkStopwatchRecord rvk_stopwatch_mark(RvkStopwatch* sw, VkCommandBuffer vkCmdBuf) {
  diag_assert_msg(!(sw->flags & RvkStopwatch_HasResults), "Stopwatch is already finished");
  if (LIKELY(sw->flags & RvkStopwatch_Supported)) {
    // Record the timestamp after all commands have completely finished executing (bottom of pipe).
    vkCmdWriteTimestamp(
        vkCmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, sw->vkQueryPool, sw->counter);
  }
  return (RvkStopwatchRecord)sw->counter++;
}
