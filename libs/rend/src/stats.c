#include "ecs/view.h"
#include "ecs/world.h"
#include "rend/stats.h"
#include "rvk/attach.h"
#include "rvk/canvas.h"
#include "rvk/desc.h"
#include "rvk/device.h"
#include "rvk/mem.h"
#include "rvk/sampler.h"
#include "rvk/swapchain.h"
#include "scene/camera.h"

#include "limiter.h"
#include "painter.h"
#include "platform.h"
#include "reset.h"
#include "resource.h"

ASSERT(rend_stats_max_passes == rvk_canvas_max_passes, "Unexpected pass count");

ecs_comp_define_public(RendStatsComp);

static void ecs_destruct_rend_stats_comp(void* data) {
  RendStatsComp* comp = data;
  alloc_free_array_t(g_allocHeap, comp->passes, rend_stats_max_passes);
  string_maybe_free(g_allocHeap, comp->gpuName);
  string_maybe_free(g_allocHeap, comp->gpuDriverName);
}

static void rend_stats_update_str(String* strPtr, const String newStr) {
  if (LIKELY(strPtr->size == newStr.size)) {
    mem_cpy(*strPtr, newStr);
    return;
  }
  if (strPtr->size) {
    alloc_free(g_allocHeap, *strPtr);
  }
  *strPtr = string_dup(g_allocHeap, newStr);
}

ecs_view_define(GlobalView) {
  ecs_access_write(RendPlatformComp);
  ecs_access_read(RendLimiterComp);
  ecs_access_without(RendResetComp);
}

ecs_view_define(UpdateStatsView) {
  ecs_access_read(RendPainterComp);
  ecs_access_with(SceneCameraComp); // Only track stats for painters with 3d content.
  ecs_access_maybe_write(RendStatsComp);
}

ecs_view_define(LoadedResourceView) {
  ecs_access_with(RendResComp);
  ecs_access_with(RendResFinishedComp);
}

static void rend_stats_update_resources(EcsWorld* world, u16 resources[RendStatsRes_Count]) {
  mem_set(mem_create(resources, sizeof(u16) * RendStatsRes_Count), 0);

  EcsView* loadedResView = ecs_world_view_t(world, LoadedResourceView);
  for (EcsIterator* itr = ecs_view_itr(loadedResView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    if (ecs_world_has_t(world, entity, RendResGraphicComp)) {
      ++resources[RendStatsRes_Graphic];
    }
    if (ecs_world_has_t(world, entity, RendResShaderComp)) {
      ++resources[RendStatsRes_Shader];
    }
    if (ecs_world_has_t(world, entity, RendResMeshComp)) {
      ++resources[RendStatsRes_Mesh];
    }
    if (ecs_world_has_t(world, entity, RendResTextureComp)) {
      ++resources[RendStatsRes_Texture];
    }
  }
}

static RendStatsComp* rend_stats_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world,
      entity,
      RendStatsComp,
      .passes = alloc_array_t(g_allocHeap, RendStatsPass, rend_stats_max_passes));
}

ecs_system_define(RendUpdateCamStatsSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  RendPlatformComp*      plat    = ecs_view_write_t(globalItr, RendPlatformComp);
  const RendLimiterComp* limiter = ecs_view_read_t(globalItr, RendLimiterComp);

  RvkCanvasStats    canvasStats;
  RvkSwapchainStats swapchainStats;

  EcsView* updateView = ecs_world_view_t(world, UpdateStatsView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const RendPainterComp* painter = ecs_view_read_t(itr, RendPainterComp);
    RendStatsComp*         stats   = ecs_view_write_t(itr, RendStatsComp);
    if (!stats) {
      stats = rend_stats_create(world, ecs_view_entity(itr));
      continue;
    }

    rvk_canvas_stats(painter->canvas, &canvasStats);
    rvk_canvas_swapchain_stats(painter->canvas, &swapchainStats);

    rend_stats_update_str(&stats->gpuName, rvk_device_name(plat->device));
    rend_stats_update_str(&stats->gpuDriverName, rvk_device_driver_name(plat->device));

    stats->swapchainPresentId  = swapchainStats.presentId;
    stats->swapchainImageCount = swapchainStats.imageCount;
    stats->waitForGpuDur       = canvasStats.waitForGpuDur;
    stats->gpuWaitDur          = canvasStats.gpuWaitDur;
    stats->gpuExecDur          = canvasStats.gpuExecDur;
    stats->gpuCopyDur          = canvasStats.gpuCopyDur;
    stats->presentAcquireDur   = swapchainStats.acquireDur;
    stats->presentEnqueueDur   = swapchainStats.presentEnqueueDur;
    stats->presentWaitDur      = swapchainStats.presentWaitDur;
    stats->limiterDur          = limiter->sleepDur;

    stats->passCount = canvasStats.passCount;
    mem_cpy(
        mem_create(stats->passes, sizeof(RendStatsPass) * rend_stats_max_passes),
        mem_create(canvasStats.passes, sizeof(RendStatsPass) * canvasStats.passCount));

    stats->memChunks       = rvk_mem_chunks(plat->device->memPool);
    stats->ramOccupied     = rvk_mem_occupied(plat->device->memPool, RvkMemLoc_Host);
    stats->ramReserved     = rvk_mem_reserved(plat->device->memPool, RvkMemLoc_Host);
    stats->vramOccupied    = rvk_mem_occupied(plat->device->memPool, RvkMemLoc_Dev);
    stats->vramReserved    = rvk_mem_reserved(plat->device->memPool, RvkMemLoc_Dev);
    stats->vramBudgetTotal = plat->device->memBudgetTotal;
    stats->vramBudgetUsed  = plat->device->memBudgetUsed;

    stats->descSetsOccupied = rvk_desc_pool_sets_occupied(plat->device->descPool);
    stats->descSetsReserved = rvk_desc_pool_sets_reserved(plat->device->descPool);
    stats->descLayouts      = rvk_desc_pool_layouts(plat->device->descPool);
    stats->samplerCount     = rvk_sampler_pool_count(plat->device->samplerPool);
    rend_stats_update_resources(world, stats->resources);

    const RvkAttachPool* attachPool = rvk_canvas_attach_pool(painter->canvas);
    stats->attachCount              = rvk_attach_pool_count(attachPool);
    stats->attachMemory             = rvk_attach_pool_memory(attachPool);

    stats->profileSupported = rvk_device_profile_supported(plat->device);
    if (stats->profileTrigger) {
      rvk_device_profile_trigger(plat->device);
      stats->profileTrigger = false;
    }

#ifdef VOLO_TRACE
    rvk_canvas_push_traces(painter->canvas);
#endif
  }
}

ecs_module_init(rend_stats_module) {
  ecs_register_comp(RendStatsComp, .destructor = ecs_destruct_rend_stats_comp);

  ecs_register_view(GlobalView);
  ecs_register_view(UpdateStatsView);
  ecs_register_view(LoadedResourceView);

  ecs_register_system(
      RendUpdateCamStatsSys,
      ecs_view_id(GlobalView),
      ecs_view_id(UpdateStatsView),
      ecs_view_id(LoadedResourceView));
}
