#include "core_alloc.h"
#include "core_array.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "device_internal.h"
#include "statrecorder_internal.h"

typedef enum {
  RvkStatRecorder_Supported  = 1 << 0,
  RvkStatRecorder_Capturing  = 1 << 1,
  RvkStatRecorder_HasResults = 1 << 2,
} RvkStatRecorderFlags;

struct sRvkStatRecorder {
  RvkDevice*           dev;
  ThreadMutex          retrieveResultsMutex;
  VkQueryPool          vkQueryPool;
  RvkStatRecorderFlags flags;
  u64                  results[RvkStatMeta_CountTotal];
};

static bool rvk_statrecorder_is_manual(const RvkStat stat) { return stat >= RvkStatMeta_CountAuto; }

static VkQueryPool rvk_querypool_create(RvkDevice* dev) {
  const VkQueryPoolCreateInfo createInfo = {
      .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType          = VK_QUERY_TYPE_PIPELINE_STATISTICS,
      .queryCount         = RvkStatMeta_CountAuto,
      .pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
                            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
                            VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
                            VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT,
  };

  diag_assert(
      bitset_count(bitset_from_var(createInfo.pipelineStatistics)) == RvkStatMeta_CountAuto);

  VkQueryPool result;
  rvk_call(vkCreateQueryPool, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static void rvk_statrecorder_retrieve_results(RvkStatRecorder* sr) {
  thread_mutex_lock(sr->retrieveResultsMutex);
  if (!(sr->flags & RvkStatRecorder_HasResults)) {
    rvk_call(
        vkGetQueryPoolResults,
        sr->dev->vkDev,
        sr->vkQueryPool,
        0,
        1,
        sizeof(sr->results),
        sr->results,
        sizeof(u64),
        VK_QUERY_RESULT_64_BIT);
    sr->flags |= RvkStatRecorder_HasResults;
  }
  thread_mutex_unlock(sr->retrieveResultsMutex);
}

RvkStatRecorder* rvk_statrecorder_create(RvkDevice* dev) {
  RvkStatRecorder* sr = alloc_alloc_t(g_alloc_heap, RvkStatRecorder);
  *sr                 = (RvkStatRecorder){
      .dev                  = dev,
      .retrieveResultsMutex = thread_mutex_create(g_alloc_heap),
  };

  if (dev->vkSupportedFeatures.pipelineStatisticsQuery) {
    sr->vkQueryPool = rvk_querypool_create(dev);
    sr->flags |= RvkStatRecorder_Supported;
  } else {
    log_w("Vulkan device does not support pipeline statistics");
  }
  return sr;
}

void rvk_statrecorder_destroy(RvkStatRecorder* sr) {
  if (sr->flags & RvkStatRecorder_Supported) {
    vkDestroyQueryPool(sr->dev->vkDev, sr->vkQueryPool, &sr->dev->vkAlloc);
  }
  thread_mutex_destroy(sr->retrieveResultsMutex);
  alloc_free_t(g_alloc_heap, sr);
}

bool rvk_statrecorder_is_supported(const RvkStatRecorder* sr) {
  return (sr->flags & RvkStatRecorder_Supported) != 0;
}

void rvk_statrecorder_reset(RvkStatRecorder* sr, VkCommandBuffer vkCmdBuf) {
  if (LIKELY(sr->flags & RvkStatRecorder_Supported)) {
    vkCmdResetQueryPool(vkCmdBuf, sr->vkQueryPool, 0, RvkStatMeta_CountAuto);
  }
  mem_set(mem_var(sr->results), 0);
  sr->flags &= ~RvkStatRecorder_HasResults;
}

u64 rvk_statrecorder_query(const RvkStatRecorder* sr, const RvkStat stat) {
  if (UNLIKELY(!(sr->flags & RvkStatRecorder_Supported))) {
    return 0;
  }

  if (!(sr->flags & RvkStatRecorder_HasResults)) {
    RvkStatRecorder* srMutable = (RvkStatRecorder*)sr;
    rvk_statrecorder_retrieve_results(srMutable);
  }

  return sr->results[stat];
}

void rvk_statrecorder_report(RvkStatRecorder* sr, const RvkStat stat, const u32 count) {
  diag_assert(rvk_statrecorder_is_manual(stat));
  /**
   * NOTE: This adding using a signed atomic operation is hacky but at the moment we don't have u64
   * atomic apis. It will work okay as long as we don't overflow i64_max.
   */
  thread_atomic_add_i64((i64*)&sr->results[stat], (i64)count);
}

void rvk_statrecorder_start(RvkStatRecorder* sr, VkCommandBuffer vkCmdBuf) {
  diag_assert(!(sr->flags & RvkStatRecorder_HasResults));
  diag_assert(!(sr->flags & RvkStatRecorder_Capturing));

  if (LIKELY(sr->flags & RvkStatRecorder_Supported)) {
    vkCmdBeginQuery(vkCmdBuf, sr->vkQueryPool, 0, 0);
  }
  sr->flags |= RvkStatRecorder_Capturing;
}

void rvk_statrecorder_stop(RvkStatRecorder* sr, VkCommandBuffer vkCmdBuf) {
  diag_assert(sr->flags & RvkStatRecorder_Capturing);

  if (LIKELY(sr->flags & RvkStatRecorder_Supported)) {
    vkCmdEndQuery(vkCmdBuf, sr->vkQueryPool, 0);
  }
  sr->flags &= ~RvkStatRecorder_Capturing;
}
