#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "device_internal.h"
#include "lib_internal.h"
#include "stopwatch_internal.h"

#define rvk_stopwatch_timestamps_max 64

typedef enum {
  RvkStopwatch_Supported          = 1 << 0,
  RvkStopwatch_HasResults         = 1 << 1,
  RvkStopwatch_CanCalibrateToHost = 1 << 2,
} RvkStopwatchFlags;

struct sRvkStopwatch {
  RvkDevice*        dev;
  ThreadMutex       retrieveResultsMutex;
  VkQueryPool       vkQueryPool;
  u32               counter;
  RvkStopwatchFlags flags;
  u64               results[rvk_stopwatch_timestamps_max];
};

static VkTimeDomainKHR rvk_timedomain_host_steady(void) {
#if defined(VOLO_LINUX)
  return VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
#elif defined(VOLO_WIN32)
  return VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
#else
#error Unsupported platform
#endif
}

static bool rvk_timedomain_can_calibrate(RvkDevice* dev, const VkTimeDomainKHR target) {
  if (!(dev->flags & RvkDeviceFlags_SupportCalibratedTimestamps)) {
    return false;
  }
  VkTimeDomainKHR supportedDomains[8];
  u32             supportedDomainCount = array_elems(supportedDomains);
  rvk_call_checked(
      dev->lib,
      getPhysicalDeviceCalibrateableTimeDomainsKHR,
      dev->vkPhysDev,
      &supportedDomainCount,
      supportedDomains);

  bool supportDevice = false, supportTarget = false;
  for (u32 i = 0; i != supportedDomainCount; ++i) {
    if (supportedDomains[i] == VK_TIME_DOMAIN_DEVICE_KHR) {
      supportDevice = true;
    }
    if (supportedDomains[i] == target) {
      supportTarget = true;
    }
  }

  return supportDevice && supportTarget;
}

static VkQueryPool rvk_querypool_create(RvkDevice* dev) {
  const VkQueryPoolCreateInfo createInfo = {
      .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType  = VK_QUERY_TYPE_TIMESTAMP,
      .queryCount = rvk_stopwatch_timestamps_max,
  };
  VkQueryPool result;
  rvk_call_checked(dev, createQueryPool, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static void rvk_stopwatch_retrieve_results(RvkStopwatch* sw) {
  thread_mutex_lock(sw->retrieveResultsMutex);
  if (!(sw->flags & RvkStopwatch_HasResults) && sw->counter) {
    rvk_call_checked(
        sw->dev,
        getQueryPoolResults,
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
  RvkStopwatch* sw = alloc_alloc_t(g_allocHeap, RvkStopwatch);

  *sw = (RvkStopwatch){
      .dev                  = dev,
      .retrieveResultsMutex = thread_mutex_create(g_allocHeap),
  };

  if (dev->vkProperties.limits.timestampComputeAndGraphics) {
    sw->vkQueryPool = rvk_querypool_create(dev);
    sw->flags |= RvkStopwatch_Supported;
  } else {
    log_w("Vulkan device does not support timestamps");
  }

  if (rvk_timedomain_can_calibrate(dev, rvk_timedomain_host_steady())) {
    sw->flags |= RvkStopwatch_CanCalibrateToHost;
  }

  return sw;
}

void rvk_stopwatch_destroy(RvkStopwatch* sw) {
  RvkDevice* dev = sw->dev;
  if (sw->flags & RvkStopwatch_Supported) {
    rvk_call(dev, destroyQueryPool, dev->vkDev, sw->vkQueryPool, &dev->vkAlloc);
  }
  thread_mutex_destroy(sw->retrieveResultsMutex);
  alloc_free_t(g_allocHeap, sw);
}

bool rvk_stopwatch_is_supported(const RvkStopwatch* sw) {
  return (sw->flags & RvkStopwatch_Supported) != 0;
}

void rvk_stopwatch_reset(RvkStopwatch* sw, VkCommandBuffer vkCmdBuf) {
  RvkDevice* dev = sw->dev;
  if (LIKELY(sw->flags & RvkStopwatch_Supported)) {
    rvk_call(dev, cmdResetQueryPool, vkCmdBuf, sw->vkQueryPool, 0, rvk_stopwatch_timestamps_max);
  }
  sw->counter = 0;
  sw->flags &= ~RvkStopwatch_HasResults;
}

TimeSteady rvk_stopwatch_query(const RvkStopwatch* sw, const RvkStopwatchRecord record) {
  diag_assert(record < rvk_stopwatch_timestamps_max);
  if (UNLIKELY(!(sw->flags & RvkStopwatch_Supported))) {
    return 0;
  }

  if (!(sw->flags & RvkStopwatch_HasResults)) {
    RvkStopwatch* swMutable = (RvkStopwatch*)sw;
    rvk_stopwatch_retrieve_results(swMutable);
  }

  return (TimeSteady)(sw->results[record] * (f64)sw->dev->vkProperties.limits.timestampPeriod);
}

RvkStopwatchRecord rvk_stopwatch_mark(RvkStopwatch* sw, VkCommandBuffer vkCmdBuf) {
  diag_assert_msg(!(sw->flags & RvkStopwatch_HasResults), "Stopwatch is already finished");
  diag_assert_msg(
      sw->counter != rvk_stopwatch_timestamps_max,
      "Maximum stopwatch records ({}) exceeded",
      fmt_int(rvk_stopwatch_timestamps_max));

  if (LIKELY(sw->flags & RvkStopwatch_Supported)) {
    // Record the timestamp after all commands have completely finished executing (bottom of pipe).
    rvk_call(
        sw->dev,
        cmdWriteTimestamp,
        vkCmdBuf,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        sw->vkQueryPool,
        sw->counter);
  }
  return (RvkStopwatchRecord)sw->counter++;
}
