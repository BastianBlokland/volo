#include "asset_manager.h"
#include "core_diag.h"
#include "debug_finder.h"
#include "ecs_view.h"
#include "ecs_world.h"

static const String g_queryPatterns[DebugFinderCategory_Count] = {
    [DebugFinderCategory_Level]   = string_static("levels/*.level"),
    [DebugFinderCategory_Terrain] = string_static("terrains/*.terrain"),
};

typedef struct {
  DebugFinderStatus status;
} DebugFinderState;

ecs_comp_define(DebugAssetFinderComp) { DebugFinderState states[DebugFinderCategory_Count]; };

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_write(DebugAssetFinderComp);
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
    finder = ecs_world_add_t(world, ecs_world_global(world), DebugAssetFinderComp);
  }
  (void)assets;
  (void)g_queryPatterns;
}

ecs_module_init(debug_finder_module) {
  ecs_register_comp(DebugAssetFinderComp);

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
  (void)finder;
  (void)cat;
  return (DebugFinderResult){0};
}
