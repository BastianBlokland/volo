#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "debug_finder.h"
#include "ecs_view.h"
#include "ecs_world.h"

static const String g_queryPatterns[DebugFinderCategory_Count] = {
    [DebugFinder_Decal]       = string_static("vfx/*.decal"),
    [DebugFinder_Graphic]     = string_static("graphics/*.graphic"),
    [DebugFinder_Level]       = string_static("levels/*.level"),
    [DebugFinder_ScriptScene] = string_static("scripts/scene/*.script"),
    [DebugFinder_Sound]       = string_static("external/sound/*.wav"),
    [DebugFinder_Terrain]     = string_static("terrains/*.terrain"),
    [DebugFinder_Vfx]         = string_static("vfx/*.vfx"),
};

typedef struct {
  DebugFinderStatus status;
  bool              executedQuery;
  DynArray          entities; // EcsEntityId[].
  DynArray          ids;      // Strings[].
} DebugFinderState;

ecs_comp_define(DebugFinderComp) { DebugFinderState states[DebugFinderCategory_Count]; };

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_write(DebugFinderComp);
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

static void ecs_destruct_finder(void* data) {
  DebugFinderComp* comp = data;
  for (DebugFinderCategory cat = 0; cat != DebugFinderCategory_Count; ++cat) {
    dynarray_destroy(&comp->states[cat].entities);
    dynarray_destroy(&comp->states[cat].ids);
  }
}

static DebugFinderComp* finder_init(EcsWorld* world, const EcsEntityId entity) {
  DebugFinderComp* finder = ecs_world_add_t(world, entity, DebugFinderComp);
  for (DebugFinderCategory cat = 0; cat != DebugFinderCategory_Count; ++cat) {
    finder->states[cat].entities = dynarray_create_t(g_allocHeap, EcsEntityId, 0);
    finder->states[cat].ids      = dynarray_create_t(g_allocHeap, String, 0);
  }
  return finder;
}

ecs_system_define(DebugFinderUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);
  DebugFinderComp*  finder = ecs_view_write_t(globalItr, DebugFinderComp);
  if (!finder) {
    finder = finder_init(world, ecs_world_global(world));
  }

  EcsView*     assetView = ecs_world_view_t(world, AssetView);
  EcsIterator* assetItr  = ecs_view_itr(assetView);

  EcsEntityId assetBuffer[asset_query_max_results];

  for (DebugFinderCategory cat = 0; cat != DebugFinderCategory_Count; ++cat) {
    DebugFinderState* state = &finder->states[cat];
    if (state->status != DebugFinderStatus_Loading) {
      continue; // No need to refresh.
    }

    // Query the asset entities.
    if (!state->executedQuery) {
      const u32 count = asset_query(world, assets, g_queryPatterns[cat], assetBuffer);
      dynarray_clear(&state->entities);
      mem_cpy(
          dynarray_push(&state->entities, count),
          mem_create(assetBuffer, count * sizeof(EcsEntityId)));
      state->executedQuery = true;
      continue; // Wait a frame before fetching the ids.
    }

    // Fetch the ids of the assets.
    dynarray_clear(&state->ids);
    dynarray_for_t(&state->entities, EcsEntityId, asset) {
      ecs_view_jump(assetItr, *asset);
      *dynarray_push_t(&state->ids, String) = asset_id(ecs_view_read_t(assetItr, AssetComp));
    }

    // Ready.
    state->status        = DebugFinderStatus_Ready;
    state->executedQuery = false;
  }
}

ecs_module_init(debug_finder_module) {
  ecs_register_comp(DebugFinderComp, .destructor = ecs_destruct_finder);

  ecs_register_view(GlobalView);
  ecs_register_view(AssetView);

  ecs_register_system(DebugFinderUpdateSys, ecs_view_id(GlobalView), ecs_view_id(AssetView));
}

void debug_finder_query(
    DebugFinderComp* finder, const DebugFinderCategory cat, const bool refresh) {
  diag_assert(cat < DebugFinderCategory_Count);

  DebugFinderState* state = &finder->states[cat];
  switch (state->status) {
  case DebugFinderStatus_Idle:
    state->status = DebugFinderStatus_Loading;
    break;
  case DebugFinderStatus_Loading:
    break;
  case DebugFinderStatus_Ready:
    if (refresh) {
      state->status = DebugFinderStatus_Loading;
    }
    break;
  }
}

DebugFinderResult debug_finder_get(DebugFinderComp* finder, const DebugFinderCategory cat) {
  diag_assert(cat < DebugFinderCategory_Count);

  DebugFinderState* state = &finder->states[cat];
  if (state->status != DebugFinderStatus_Ready) {
    return (DebugFinderResult){.status = state->status};
  }
  return (DebugFinderResult){
      .status   = DebugFinderStatus_Ready,
      .count    = (u32)state->entities.size,
      .entities = dynarray_begin_t(&state->entities, EcsEntityId),
      .ids      = dynarray_begin_t(&state->ids, String),
  };
}
