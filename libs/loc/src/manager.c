#include "asset/manager.h"
#include "core/alloc.h"
#include "core/array.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "loc/manager.h"

typedef enum {
  LocEntry_Acquired  = 1 << 0,
  LocEntry_Unloading = 1 << 1,
} LocEntryFlags;

typedef struct {
  LocEntryFlags flags;
  EcsEntityId   asset;
} LocEntry;

ecs_comp_define(LocManagerComp) {
  String preferredLocale;

  bool entriesInit;
  HeapArray_t(LocEntry) entries;
};

static void ecs_destruct_loc_manager(void* data) {
  LocManagerComp* comp = data;
  string_maybe_free(g_allocHeap, comp->preferredLocale);

  if (comp->entries.values) {
    alloc_free_array_t(g_allocHeap, comp->entries.values, comp->entries.count);
  }
}

static void loc_entries_init(EcsWorld* world, LocManagerComp* man, AssetManagerComp* assets) {
  const String assetPattern = string_lit("locale/*.locale");
  EcsEntityId  assetEntities[asset_query_max_results];
  const u32    assetCount = asset_query(world, assets, assetPattern, assetEntities);
  if (assetCount) {
    man->entries.count  = assetCount;
    man->entries.values = alloc_array_t(g_allocHeap, LocEntry, assetCount);
    for (u32 i = 0; i != assetCount; ++i) {
      asset_acquire(world, assetEntities[i]);
      man->entries.values[i] = (LocEntry){
          .flags = LocEntry_Acquired,
          .asset = assetEntities[i],
      };
    }
  }
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(LocManagerComp);
}

ecs_system_define(LocUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  LocManagerComp*   man    = ecs_view_write_t(globalItr, LocManagerComp);
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  if (!man->entriesInit) {
    loc_entries_init(world, man, assets);
    man->entriesInit = true;
    return;
  }
}

ecs_module_init(loc_manager_module) {
  ecs_register_comp(LocManagerComp, .destructor = ecs_destruct_loc_manager);

  ecs_register_view(UpdateGlobalView);

  ecs_register_system(LocUpdateSys, ecs_view_id(UpdateGlobalView));
}

LocManagerComp* loc_manager_init(EcsWorld* world, const String preferredLocale) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      LocManagerComp,
      .preferredLocale = string_maybe_dup(g_allocHeap, preferredLocale));
}
