#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "debug_finder.h"
#include "ecs_view.h"
#include "ecs_world.h"

static const String g_queryPatterns[DebugFinderCategory_Count] = {
    [DebugFinderCategory_Level]   = string_static("levels/*.level"),
    [DebugFinderCategory_Terrain] = string_static("terrains/*.terrain"),
};

typedef struct {
  DebugFinderStatus status;
  DynArray          entities; // EcsEntityId[].
  DynArray          ids;      // Strings[].
} DebugFinderState;

ecs_comp_define(DebugAssetFinderComp) { DebugFinderState states[DebugFinderCategory_Count]; };

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_write(DebugAssetFinderComp);
}

static void ecs_destruct_finder(void* data) {
  DebugAssetFinderComp* comp = data;
  for (DebugFinderCategory cat = 0; cat != DebugFinderCategory_Count; ++cat) {
    dynarray_destroy(&comp->states[cat].entities);
    dynarray_destroy(&comp->states[cat].ids);
  }
}

static DebugAssetFinderComp* finder_init(EcsWorld* world, const EcsEntityId entity) {
  DebugAssetFinderComp* f = ecs_world_add_t(world, entity, DebugAssetFinderComp);
  for (DebugFinderCategory cat = 0; cat != DebugFinderCategory_Count; ++cat) {
    f->states[cat].entities = dynarray_create_t(g_allocHeap, EcsEntityId, 0);
    f->states[cat].ids      = dynarray_create_t(g_allocHeap, EcsEntityId, 0);
  }
  return f;
}

ecs_system_define(DebugFinderUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  AssetManagerComp*     assets = ecs_view_write_t(globalItr, AssetManagerComp);
  DebugAssetFinderComp* finder = ecs_view_write_t(globalItr, DebugAssetFinderComp);
  if (!finder) {
    finder = finder_init(world, ecs_world_global(world));
  }
  (void)assets;
  (void)g_queryPatterns;
}

ecs_module_init(debug_finder_module) {
  ecs_register_comp(DebugAssetFinderComp, .destructor = ecs_destruct_finder);

  ecs_register_view(GlobalView);

  ecs_register_system(DebugFinderUpdateSys, ecs_view_id(GlobalView));
}

void debug_asset_query(
    DebugAssetFinderComp* finder, const DebugFinderCategory cat, const bool refresh) {
  diag_assert(cat < DebugFinderCategory_Count);

  if (finder->states[cat].status == DebugFinderStatus_Idle || refresh) {
    finder->states[cat].status = DebugFinderStatus_Loading;
  }
}

DebugFinderResult debug_finder_get(DebugAssetFinderComp* finder, const DebugFinderCategory cat) {
  diag_assert(cat < DebugFinderCategory_Count);

  return (DebugFinderResult){
      .status   = finder->states[cat].status,
      .count    = (u32)finder->states[cat].entities.size,
      .entities = dynarray_begin_t(&finder->states[cat].entities, EcsEntityId),
      .ids      = dynarray_begin_t(&finder->states[cat].ids, String),
  };
}
