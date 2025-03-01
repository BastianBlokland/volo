#include "core.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "device_internal.h"
#include "lib_internal.h"
#include "statrecorder_internal.h"

#define rvk_statrecorder_queries_max 64

typedef enum {
  RvkStatRecorder_Supported   = 1 << 0,
  RvkStatRecorder_Capturing   = 1 << 1,
  RvkStatRecorder_HasCaptured = 1 << 2,
  RvkStatRecorder_HasResults  = 1 << 3,
} RvkStatRecorderFlags;

struct sRvkStatRecorder {
  RvkDevice*           dev;
  ThreadMutex          retrieveResultsMutex;
  VkQueryPool          vkQueryPool;
  u16                  counter;
  RvkStatRecorderFlags flags;
  u64                  results[rvk_statrecorder_queries_max * RvkStat_Count];
};

static VkQueryPool rvk_querypool_create(RvkDevice* dev) {
  const VkQueryPoolCreateInfo createInfo = {
      .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType          = VK_QUERY_TYPE_PIPELINE_STATISTICS,
      .queryCount         = rvk_statrecorder_queries_max,
      .pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
                            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
                            VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
                            VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT,
  };

  diag_assert(bitset_count(bitset_from_var(createInfo.pipelineStatistics)) == RvkStat_Count);

  VkQueryPool result;
  rvk_call_checked(dev, createQueryPool, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static void rvk_statrecorder_retrieve_results(RvkStatRecorder* sr) {
  thread_mutex_lock(sr->retrieveResultsMutex);
  if (!(sr->flags & RvkStatRecorder_HasResults)) {
    const VkResult vkQueryRes = sr->dev->api.getQueryPoolResults(
        sr->dev->vkDev,
        sr->vkQueryPool,
        0,
        sr->counter,
        sizeof(sr->results),
        sr->results,
        sizeof(u64) * RvkStat_Count,
        VK_QUERY_RESULT_64_BIT);
    if (vkQueryRes == VK_NOT_READY) {
      mem_set(array_mem(sr->results), 0);
    } else {
      rvk_api_check(string_lit("getQueryPoolResults"), vkQueryRes);
    }
    sr->flags |= RvkStatRecorder_HasResults;
  }
  thread_mutex_unlock(sr->retrieveResultsMutex);
}

RvkStatRecorder* rvk_statrecorder_create(RvkDevice* dev) {
  RvkStatRecorder* sr = alloc_alloc_t(g_allocHeap, RvkStatRecorder);

  *sr = (RvkStatRecorder){
      .dev                  = dev,
      .retrieveResultsMutex = thread_mutex_create(g_allocHeap),
  };

  if (dev->flags & RvkDeviceFlags_SupportPipelineStatQuery) {
    sr->vkQueryPool = rvk_querypool_create(dev);
    sr->flags |= RvkStatRecorder_Supported;
  } else {
    log_w("Vulkan device does not support pipeline statistics");
  }
  return sr;
}

void rvk_statrecorder_destroy(RvkStatRecorder* sr) {
  if (sr->flags & RvkStatRecorder_Supported) {
    sr->dev->api.destroyQueryPool(sr->dev->vkDev, sr->vkQueryPool, &sr->dev->vkAlloc);
  }
  thread_mutex_destroy(sr->retrieveResultsMutex);
  alloc_free_t(g_allocHeap, sr);
}

bool rvk_statrecorder_is_supported(const RvkStatRecorder* sr) {
  return (sr->flags & RvkStatRecorder_Supported) != 0;
}

void rvk_statrecorder_reset(RvkStatRecorder* sr, VkCommandBuffer vkCmdBuf) {
  if (LIKELY(sr->flags & RvkStatRecorder_Supported)) {
    sr->dev->api.cmdResetQueryPool(vkCmdBuf, sr->vkQueryPool, 0, rvk_statrecorder_queries_max);
  }
  sr->counter = 0;
  sr->flags &= ~RvkStatRecorder_HasResults;
  mem_set(array_mem(sr->results), 0);
}

u64 rvk_statrecorder_query(
    const RvkStatRecorder* sr, const RvkStatRecord record, const RvkStat stat) {
  diag_assert(record < rvk_statrecorder_queries_max);
  diag_assert_msg(
      sr->flags & RvkStatRecorder_HasCaptured,
      "Unable to query recorder: No stats have been captured yet");

  if (UNLIKELY(!(sr->flags & RvkStatRecorder_Supported))) {
    return 0;
  }

  if (!(sr->flags & RvkStatRecorder_HasResults)) {
    RvkStatRecorder* srMutable = (RvkStatRecorder*)sr;
    rvk_statrecorder_retrieve_results(srMutable);
  }

  return sr->results[record * RvkStat_Count + stat];
}

RvkStatRecord rvk_statrecorder_start(RvkStatRecorder* sr, VkCommandBuffer vkCmdBuf) {
  diag_assert(!(sr->flags & RvkStatRecorder_HasResults));
  diag_assert(!(sr->flags & RvkStatRecorder_Capturing));
  diag_assert_msg(
      sr->counter != rvk_statrecorder_queries_max,
      "Maximum statrecorder records ({}) exceeded",
      fmt_int(rvk_statrecorder_queries_max));

  if (LIKELY(sr->flags & RvkStatRecorder_Supported)) {
    sr->dev->api.cmdBeginQuery(vkCmdBuf, sr->vkQueryPool, sr->counter, 0);
  }
  sr->flags |= RvkStatRecorder_Capturing;
  return (RvkStatRecord)sr->counter++;
}

void rvk_statrecorder_stop(
    RvkStatRecorder* sr, const RvkStatRecord record, VkCommandBuffer vkCmdBuf) {
  diag_assert(record < rvk_statrecorder_queries_max);
  diag_assert(sr->flags & RvkStatRecorder_Capturing);

  if (LIKELY(sr->flags & RvkStatRecorder_Supported)) {
    sr->dev->api.cmdEndQuery(vkCmdBuf, sr->vkQueryPool, record);
  }
  sr->flags &= ~RvkStatRecorder_Capturing;
  sr->flags |= RvkStatRecorder_HasCaptured;
}
