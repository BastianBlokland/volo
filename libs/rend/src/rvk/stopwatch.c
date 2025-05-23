#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "device_internal.h"
#include "lib_internal.h"
#include "stopwatch_internal.h"

#if defined(VOLO_LINUX)
#define rvk_timedomain_host VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR
#elif defined(VOLO_WIN32)
#define rvk_timedomain_host VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR
#else
#error Unsupported platform
#endif

#define rvk_stopwatch_timestamps_max 64
#define rvk_stopwatch_calibration_max_deviation time_microseconds(50)
#define rvk_stopwatch_calibration_max_tries 3
#define rvk_stopwatch_calibration_timeout time_minutes(30)

typedef enum {
  RvkStopwatch_Supported      = 1 << 0,
  RvkStopwatch_HasResults     = 1 << 1,
  RvkStopwatch_CanCalibrate   = 1 << 2,
  RvkStopwatch_HasCalibration = 1 << 3,
} RvkStopwatchFlags;

struct sRvkStopwatch {
  RvkDevice*        dev;
  ThreadMutex       retrieveResultsMutex;
  VkQueryPool       vkQueryPool;
  u32               counter;
  RvkStopwatchFlags flags;
  TimeSteady        calibrationDevice, calibrationHost;
  u64               results[rvk_stopwatch_timestamps_max];
};

static bool rvk_stopwatch_can_calibrate(RvkStopwatch* sw) {
  if (!(sw->dev->flags & RvkDeviceFlags_SupportCalibratedTimestamps)) {
    return false;
  }
  VkTimeDomainKHR supportedDomains[8];
  u32             supportedDomainCount = array_elems(supportedDomains);
  rvk_call_checked(
      sw->dev->lib,
      getPhysicalDeviceCalibrateableTimeDomainsKHR,
      sw->dev->vkPhysDev,
      &supportedDomainCount,
      supportedDomains);

  bool supportDevice = false, supportHost = false;
  for (u32 i = 0; i != supportedDomainCount; ++i) {
    if (supportedDomains[i] == VK_TIME_DOMAIN_DEVICE_KHR) {
      supportDevice = true;
    }
    if (supportedDomains[i] == rvk_timedomain_host) {
      supportHost = true;
    }
  }

  return supportDevice && supportHost;
}

static void rvk_stopwatch_calibrate(RvkStopwatch* sw) {
  if (!(sw->flags & RvkStopwatch_CanCalibrate)) {
    sw->flags &= ~RvkStopwatch_HasCalibration;
    return; // Calibration not supported.
  }

  const VkCalibratedTimestampInfoKHR timestampInfos[2] = {
      {
          .sType      = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
          .timeDomain = VK_TIME_DOMAIN_DEVICE_KHR,
      },
      {
          .sType      = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
          .timeDomain = rvk_timedomain_host,
      },
  };
  u64 timestamps[2];
  u64 maxDeviation = 0;
  u32 numTries     = 1;

Retry:
  rvk_call_checked(
      sw->dev,
      getCalibratedTimestampsKHR,
      sw->dev->vkDev,
      array_elems(timestamps),
      timestampInfos,
      timestamps,
      &maxDeviation);

  if (maxDeviation > rvk_stopwatch_calibration_max_deviation) {
    // Calibration too imprecise; attempt to get a more accurate calibration.
    if (numTries++ <= rvk_stopwatch_calibration_max_tries) {
      goto Retry;
    }
    log_w("GPU stopwatch calibration failed", log_param("deviation", fmt_int(maxDeviation)));

    sw->calibrationHost   = time_steady_clock(); // Mark the timestamp of the calibration attempt.
    sw->calibrationDevice = 0;
    sw->flags &= ~RvkStopwatch_HasCalibration;
    return;
  }

  sw->calibrationDevice = (TimeSteady)timestamps[0];
  sw->calibrationHost   = (TimeSteady)timestamps[1];
  sw->flags |= RvkStopwatch_HasCalibration;
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

  if (rvk_stopwatch_can_calibrate(sw)) {
    sw->flags |= RvkStopwatch_CanCalibrate;
    rvk_stopwatch_calibrate(sw);
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

bool rvk_stopwatch_calibrated(const RvkStopwatch* sw) {
  return (sw->flags & RvkStopwatch_HasCalibration) != 0;
}

void rvk_stopwatch_reset(RvkStopwatch* sw, VkCommandBuffer vkCmdBuf) {
  RvkDevice* dev = sw->dev;
  if (LIKELY(sw->flags & RvkStopwatch_Supported)) {
    rvk_call(dev, cmdResetQueryPool, vkCmdBuf, sw->vkQueryPool, 0, rvk_stopwatch_timestamps_max);
  }
  sw->counter = 0;
  sw->flags &= ~RvkStopwatch_HasResults;

  const TimeDuration calibrationAge = time_steady_clock() - sw->calibrationHost;
  if (sw->flags & RvkStopwatch_CanCalibrate && calibrationAge > rvk_stopwatch_calibration_timeout) {
    rvk_stopwatch_calibrate(sw);
  }
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

  const f64 timestampPeriod = sw->dev->vkProperties.limits.timestampPeriod;

  if (sw->flags & RvkStopwatch_HasCalibration) {
    const TimeDuration offsetRaw = sw->results[record] - sw->calibrationDevice;
    const TimeDuration offset    = (TimeDuration)(offsetRaw * timestampPeriod);
    return sw->calibrationHost + offset;
  }

  return (TimeSteady)(sw->results[record] * timestampPeriod);
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
