#include "ecs_world.h"
#include "rend_register.h"
#include "rend_stats.h"

#include "painter_internal.h"
#include "platform_internal.h"
#include "rvk/canvas_internal.h"
#include "rvk/device_internal.h"
#include "rvk/mem_internal.h"

ecs_comp_define_public(RendStatsComp);

ecs_view_define(GlobalView) { ecs_access_read(RendPlatformComp); }

ecs_view_define(UpdateStatsView) {
  ecs_access_read(RendPainterComp);
  ecs_access_maybe_write(RendStatsComp);
}

ecs_system_define(RendUpdateStatsSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const RendPlatformComp* plat = ecs_view_read_t(globalItr, RendPlatformComp);

  EcsView* updateView = ecs_world_view_t(world, UpdateStatsView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const RendPainterComp* painter = ecs_view_read_t(itr, RendPainterComp);
    RendStatsComp*         stats   = ecs_view_write_t(itr, RendStatsComp);
    if (!stats) {
      stats = ecs_world_add_t(world, ecs_view_entity(itr), RendStatsComp);
    }
    // NOTE: Will block until the previous draw has finished.
    const RvkRenderStats renderStats = rvk_canvas_stats(painter->canvas);

    stats->renderResolution = rvk_canvas_size(painter->canvas);
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
  }
}

ecs_module_init(rend_stats_module) {
  ecs_register_comp(RendStatsComp);

  ecs_register_view(GlobalView);
  ecs_register_view(UpdateStatsView);

  ecs_register_system(RendUpdateStatsSys, ecs_view_id(GlobalView), ecs_view_id(UpdateStatsView));
  ecs_order(RendUpdateStatsSys, RendOrder_DrawExecute - 1);
}
