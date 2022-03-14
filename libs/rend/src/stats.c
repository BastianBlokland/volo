#include "ecs_world.h"
#include "rend_register.h"
#include "rend_stats.h"

#include "painter_internal.h"
#include "platform_internal.h"
#include "resource_internal.h"
#include "rvk/canvas_internal.h"
#include "rvk/desc_internal.h"
#include "rvk/device_internal.h"
#include "rvk/mem_internal.h"

ecs_comp_define_public(RendStatsComp);

static void ecs_destruct_rend_stats_comp(void* data) {
  RendStatsComp* comp = data;
  if (!string_is_empty(comp->gpuName)) {
    string_free(g_alloc_heap, comp->gpuName);
  }
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

ecs_view_define(GlobalView) { ecs_access_read(RendPlatformComp); }

ecs_view_define(UpdateStatsView) {
  ecs_access_read(RendPainterComp);
  ecs_access_maybe_write(RendStatsComp);
}

ecs_view_define(LoadedResourceView) {
  ecs_access_with(RendResComp);
  ecs_access_with(RendResFinishedComp);
}

static const RendPlatformComp* rend_stats_platform(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_read_t(globalItr, RendPlatformComp) : null;
}

static void rend_stat_update_resources(EcsWorld* world, u32 resources[RendStatRes_Count]) {
  mem_set(mem_create(resources, sizeof(u32) * RendStatRes_Count), 0);

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
  const RendPlatformComp* plat = rend_stats_platform(world);
  if (!plat) {
    return;
  }

  EcsView* updateView = ecs_world_view_t(world, UpdateStatsView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const RendPainterComp* painter = ecs_view_read_t(itr, RendPainterComp);
    RendStatsComp*         stats   = ecs_view_write_t(itr, RendStatsComp);
    if (!stats) {
      ecs_world_add_t(world, ecs_view_entity(itr), RendStatsComp);
      continue;
    }

    // NOTE: Will block until the previous draw has finished.
    const RvkRenderStats renderStats = rvk_canvas_stats(painter->canvas);

    rend_stats_update_str(&stats->gpuName, rvk_device_name(plat->device));
    stats->renderSize[0]    = renderStats.forwardResolution.width;
    stats->renderSize[1]    = renderStats.forwardResolution.height;
    stats->draws            = renderStats.forwardDraws;
    stats->instances        = renderStats.forwardInstances;
    stats->renderTime       = renderStats.renderTime;
    stats->vertices         = renderStats.forwardVertices;
    stats->primitives       = renderStats.forwardPrimitives;
    stats->shadersVert      = renderStats.forwardShadersVert;
    stats->shadersFrag      = renderStats.forwardShadersFrag;
    stats->ramOccupied      = rvk_mem_occupied(plat->device->memPool, RvkMemLoc_Host);
    stats->ramReserved      = rvk_mem_reserved(plat->device->memPool, RvkMemLoc_Host);
    stats->vramOccupied     = rvk_mem_occupied(plat->device->memPool, RvkMemLoc_Dev);
    stats->vramReserved     = rvk_mem_reserved(plat->device->memPool, RvkMemLoc_Dev);
    stats->descSetsOccupied = rvk_desc_pool_sets_occupied(plat->device->descPool);
    stats->descSetsReserved = rvk_desc_pool_sets_reserved(plat->device->descPool);
    stats->descLayouts      = rvk_desc_pool_layouts(plat->device->descPool);
    rend_stat_update_resources(world, stats->resources);
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
  ecs_order(RendUpdateCamStatsSys, RendOrder_DrawExecute - 1);
}
