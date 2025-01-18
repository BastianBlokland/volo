#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "dev_finder.h"
#include "ecs_view.h"
#include "ecs_world.h"

static const String g_queryPatterns[DevFinderCategory_Count] = {
    [DevFinder_Decal]   = string_static("vfx/*.decal"),
    [DevFinder_Graphic] = string_static("graphics/*.graphic"),
    [DevFinder_Level]   = string_static("levels/*.level"),
    [DevFinder_Sound]   = string_static("external/sound/*.wav"),
    [DevFinder_Terrain] = string_static("terrains/*.terrain"),
    [DevFinder_Vfx]     = string_static("vfx/*.vfx"),
};

const String g_debugFinderCategoryNames[] = {
    [DevFinder_Decal]   = string_static("Decal"),
    [DevFinder_Graphic] = string_static("Graphic"),
    [DevFinder_Level]   = string_static("Level"),
    [DevFinder_Sound]   = string_static("Sound"),
    [DevFinder_Terrain] = string_static("Terrain"),
    [DevFinder_Vfx]     = string_static("Vfx"),
};
ASSERT(array_elems(g_debugFinderCategoryNames) == DevFinderCategory_Count, "Missing names");

typedef struct {
  DevFinderStatus status;
  bool            executedQuery;
  DynArray        entities; // EcsEntityId[].
  DynArray        ids;      // Strings[].
} DevFinderState;

ecs_comp_define(DevFinderComp) { DevFinderState* states; };

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_write(DevFinderComp);
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

static void ecs_destruct_finder(void* data) {
  DevFinderComp* comp = data;
  for (DevFinderCategory cat = 0; cat != DevFinderCategory_Count; ++cat) {
    dynarray_destroy(&comp->states[cat].entities);
    dynarray_destroy(&comp->states[cat].ids);
  }
  alloc_free_array_t(g_allocHeap, comp->states, DevFinderCategory_Count);
}

static DevFinderComp* finder_init(EcsWorld* world, const EcsEntityId entity) {
  DevFinderComp* finder = ecs_world_add_t(world, entity, DevFinderComp);
  finder->states        = alloc_array_t(g_allocHeap, DevFinderState, DevFinderCategory_Count);
  for (DevFinderCategory cat = 0; cat != DevFinderCategory_Count; ++cat) {
    finder->states[cat] = (DevFinderState){
        .entities = dynarray_create_t(g_allocHeap, EcsEntityId, 0),
        .ids      = dynarray_create_t(g_allocHeap, String, 0),
    };
  }
  return finder;
}

ecs_system_define(DevFinderUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);
  DevFinderComp*    finder = ecs_view_write_t(globalItr, DevFinderComp);
  if (!finder) {
    finder = finder_init(world, ecs_world_global(world));
  }

  EcsView*     assetView = ecs_world_view_t(world, AssetView);
  EcsIterator* assetItr  = ecs_view_itr(assetView);

  EcsEntityId assetBuffer[asset_query_max_results];

  for (DevFinderCategory cat = 0; cat != DevFinderCategory_Count; ++cat) {
    DevFinderState* state = &finder->states[cat];
    if (state->status != DevFinderStatus_Loading) {
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
    state->status        = DevFinderStatus_Ready;
    state->executedQuery = false;
  }
}

ecs_module_init(dev_finder_module) {
  ecs_register_comp(DevFinderComp, .destructor = ecs_destruct_finder);

  ecs_register_view(GlobalView);
  ecs_register_view(AssetView);

  ecs_register_system(DevFinderUpdateSys, ecs_view_id(GlobalView), ecs_view_id(AssetView));
}

void dev_finder_query(DevFinderComp* finder, const DevFinderCategory cat, const bool refresh) {
  diag_assert(cat < DevFinderCategory_Count);

  DevFinderState* state = &finder->states[cat];
  switch (state->status) {
  case DevFinderStatus_Idle:
    state->status = DevFinderStatus_Loading;
    break;
  case DevFinderStatus_Loading:
    break;
  case DevFinderStatus_Ready:
    if (refresh) {
      state->status = DevFinderStatus_Loading;
    }
    break;
  }
}

DevFinderResult dev_finder_get(DevFinderComp* finder, const DevFinderCategory cat) {
  diag_assert(cat < DevFinderCategory_Count);

  DevFinderState* state = &finder->states[cat];
  if (state->status != DevFinderStatus_Ready) {
    return (DevFinderResult){.status = state->status};
  }
  return (DevFinderResult){
      .status   = DevFinderStatus_Ready,
      .count    = (u32)state->entities.size,
      .entities = dynarray_begin_t(&state->entities, EcsEntityId),
      .ids      = dynarray_begin_t(&state->ids, String),
  };
}
