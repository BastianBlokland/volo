#include "asset_manager.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "rend_draw.h"
#include "scene_lifetime.h"

static const u32 g_gridSegments          = 400;
static const u32 g_gridHighlightInterval = 5;
static const f32 g_gridCellSizeDefault   = 1.0f;

typedef struct {
  ALIGNAS(16)
  f32 cellSize;
  u32 segments;
  u32 highlightInterval;
} DebugGridData;

ASSERT(sizeof(DebugGridData) == 16, "Size needs to match the size defined in glsl");

ecs_comp_define(DebugGridComp) {
  f32         cellSize;
  EcsEntityId draw;
};

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(GridCreateView) {
  ecs_access_with(GapWindowComp);
  ecs_access_without(DebugGridComp);
}

ecs_view_define(GridReadView) { ecs_access_read(DebugGridComp); }
ecs_view_define(DrawWriteView) { ecs_access_write(RendDrawComp); }

static AssetManagerComp* debug_grid_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static void debug_grid_create(EcsWorld* world, const EcsEntityId entity, AssetManagerComp* assets) {
  const EcsEntityId drawEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, drawEntity, SceneLifetimeOwnerComp, .owner = entity);

  RendDrawComp* draw = rend_draw_create(world, drawEntity, RendDrawFlags_NoInstanceFiltering);
  rend_draw_set_graphic(draw, asset_lookup(world, assets, string_lit("graphics/debug/grid.gra")));
  rend_draw_set_vertex_count(draw, g_gridSegments * 4);

  ecs_world_add_t(
      world, entity, DebugGridComp, .cellSize = g_gridCellSizeDefault, .draw = drawEntity);
}

ecs_system_define(DebugGridCreateSys) {
  AssetManagerComp* assets = debug_grid_asset_manager(world);
  if (!assets) {
    return;
  }
  EcsView* createView = ecs_world_view_t(world, GridCreateView);
  for (EcsIterator* itr = ecs_view_itr(createView); ecs_view_walk(itr);) {
    const EcsEntityId windowEntity = ecs_view_entity(itr);
    debug_grid_create(world, windowEntity, assets);
  }
}

ecs_system_define(DebugGridDrawSys) {
  EcsIterator* drawItr = ecs_view_itr(ecs_world_view_t(world, DrawWriteView));

  EcsView* gridView = ecs_world_view_t(world, GridReadView);
  for (EcsIterator* itr = ecs_view_itr(gridView); ecs_view_walk(itr);) {
    const DebugGridComp* grid = ecs_view_read_t(itr, DebugGridComp);

    ecs_view_jump(drawItr, grid->draw);
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

    const DebugGridData data = {
        .cellSize          = grid->cellSize,
        .segments          = g_gridSegments,
        .highlightInterval = g_gridHighlightInterval,
    };
    rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, (GeoBox){0});
  }
}

ecs_module_init(debug_grid_module) {
  ecs_register_comp(DebugGridComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GridCreateView);
  ecs_register_view(GridReadView);
  ecs_register_view(DrawWriteView);

  ecs_register_system(
      DebugGridCreateSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GridCreateView));
  ecs_register_system(DebugGridDrawSys, ecs_view_id(GridReadView), ecs_view_id(DrawWriteView));
}
