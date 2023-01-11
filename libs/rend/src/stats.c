#include "core_array.h"
#include "ecs_world.h"
#include "rend_register.h"
#include "rend_stats.h"

#include "limiter_internal.h"
#include "painter_internal.h"
#include "platform_internal.h"
#include "reset_internal.h"
#include "resource_internal.h"
#include "rvk/attach_internal.h"
#include "rvk/canvas_internal.h"
#include "rvk/desc_internal.h"
#include "rvk/device_internal.h"
#include "rvk/mem_internal.h"
#include "rvk/swapchain_internal.h"

ecs_comp_define_public(RendStatsComp);

static void ecs_destruct_rend_stats_comp(void* data) {
  RendStatsComp* comp = data;
  string_maybe_free(g_alloc_heap, comp->gpuName);
}

static void rend_stats_update_str(String* strPtr, const String newStr) {
  if (LIKELY(strPtr->size == newStr.size)) {
    mem_cpy(*strPtr, newStr);
    return;
  }
  if (strPtr->size) {
    alloc_free(g_alloc_heap, *strPtr);
  }
  *strPtr = string_dup(g_alloc_heap, newStr);
}

ecs_view_define(GlobalView) {
  ecs_access_read(RendPlatformComp);
  ecs_access_read(RendLimiterComp);
  ecs_access_without(RendResetComp);
}

ecs_view_define(UpdateStatsView) {
  ecs_access_read(RendPainterComp);
  ecs_access_maybe_write(RendStatsComp);
}

ecs_view_define(LoadedResourceView) {
  ecs_access_with(RendResComp);
  ecs_access_with(RendResFinishedComp);
}

static void rend_stat_update_resources(EcsWorld* world, u16 resources[RendStatRes_Count]) {
  mem_set(mem_create(resources, sizeof(u16) * RendStatRes_Count), 0);

  EcsView* loadedResView = ecs_world_view_t(world, LoadedResourceView);
  for (EcsIterator* itr = ecs_view_itr(loadedResView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    if (ecs_world_has_t(world, entity, RendResGraphicComp)) {
      ++resources[RendStatRes_Graphic];
    }
    if (ecs_world_has_t(world, entity, RendResShaderComp)) {
      ++resources[RendStatRes_Shader];
    }
    if (ecs_world_has_t(world, entity, RendResMeshComp)) {
      ++resources[RendStatRes_Mesh];
    }
    if (ecs_world_has_t(world, entity, RendResTextureComp)) {
      ++resources[RendStatRes_Texture];
    }
  }
}

ecs_system_define(RendUpdateCamStatsSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const RendPlatformComp* plat    = ecs_view_read_t(globalItr, RendPlatformComp);
  const RendLimiterComp*  limiter = ecs_view_read_t(globalItr, RendLimiterComp);

  EcsView* updateView = ecs_world_view_t(world, UpdateStatsView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const RendPainterComp* painter = ecs_view_read_t(itr, RendPainterComp);
    RendStatsComp*         stats   = ecs_view_write_t(itr, RendStatsComp);
    if (!stats) {
      ecs_world_add_t(world, ecs_view_entity(itr), RendStatsComp);
      continue;
    }

    // NOTE: Can potentially block if the previous draw has not finished.
    const RvkCanvasStats    canvasStats    = rvk_canvas_stats(painter->canvas);
    const RvkSwapchainStats swapchainStats = rvk_canvas_swapchain_stats(painter->canvas);

    rend_stats_update_str(&stats->gpuName, rvk_device_name(plat->device));

    stats->renderDur         = canvasStats.renderDur;
    stats->waitForRenderDur  = canvasStats.waitForRenderDur;
    stats->presentAcquireDur = swapchainStats.acquireDur;
    stats->presentEnqueueDur = swapchainStats.presentEnqueueDur;
    stats->presentWaitDur    = swapchainStats.presentWaitDur;
    stats->limiterDur        = limiter->sleepDur;

    mem_cpy(array_mem(stats->passes), array_mem(canvasStats.passes));

    stats->memChunks    = rvk_mem_chunks(plat->device->memPool);
    stats->ramOccupied  = rvk_mem_occupied(plat->device->memPool, RvkMemLoc_Host);
    stats->ramReserved  = rvk_mem_reserved(plat->device->memPool, RvkMemLoc_Host);
    stats->vramOccupied = rvk_mem_occupied(plat->device->memPool, RvkMemLoc_Dev);
    stats->vramReserved = rvk_mem_reserved(plat->device->memPool, RvkMemLoc_Dev);

    stats->descSetsOccupied = rvk_desc_pool_sets_occupied(plat->device->descPool);
    stats->descSetsReserved = rvk_desc_pool_sets_reserved(plat->device->descPool);
    stats->descLayouts      = rvk_desc_pool_layouts(plat->device->descPool);
    stats->attachCount      = rvk_canvas_attach_count(painter->canvas);
    stats->attachMemory     = rvk_canvas_attach_memory(painter->canvas);
    rend_stat_update_resources(world, stats->resources);
  }
}

ecs_module_init(rend_stats_module) {
  ecs_register_comp(RendStatsComp, .destructor = ecs_destruct_rend_stats_comp);

  ecs_register_view(GlobalView);
  ecs_register_view(UpdateStatsView);
  ecs_register_view(LoadedResourceView);

  /**
   * NOTE: The update-camera-stats has to be run with the 'Exclusive' flag as we want to force it to
   * run near the end of the frame even though it has minimal dependency conflicts with other
   * systems. Reason is the renderer stat collection will block if the last frame is still in-flight
   * on the gpu, and its wasteful to block an executor while it could do other meaningful work.
   * TODO: Rethink the stat collection so it never needs to block.
   */
  ecs_register_system_with_flags(
      RendUpdateCamStatsSys,
      EcsSystemFlags_Exclusive,
      ecs_view_id(GlobalView),
      ecs_view_id(UpdateStatsView),
      ecs_view_id(LoadedResourceView));
  ecs_order(RendUpdateCamStatsSys, RendOrder_DrawExecute - 1);
}
