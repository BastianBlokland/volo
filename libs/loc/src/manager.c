#include "asset/locale.h"
#include "asset/manager.h"
#include "core/alloc.h"
#include "core/diag.h"
#include "core/path.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "loc/manager.h"
#include "log/logger.h"

#include "translate.h"

typedef enum {
  LocManagerState_Init,
  LocManagerState_Loading,
  LocManagerState_Ready,
} LocManagerState;

typedef enum {
  LocManagerEntry_Initialized = 1 << 0,
  LocManagerEntry_Acquired    = 1 << 1,
  LocManagerEntry_Unloading   = 1 << 2,
  LocManagerEntry_Failed      = 1 << 3,
  LocManagerEntry_Default     = 1 << 4,
} LocManagerEntryFlags;

typedef struct {
  LocManagerEntryFlags flags;
  EcsEntityId          asset;
  String               id; // Allocated in the asset component.
} LocManagerEntry;

ecs_comp_define(LocManagerComp) {
  String          preferredId;
  LocManagerState state;

  u32              localeActive; // Index of active locale or sentinel_u32.
  u32              localeCount;
  LocManagerEntry* localeEntries;
  String*          localeNames;
};

static void ecs_destruct_loc_manager(void* data) {
  LocManagerComp* comp = data;
  string_maybe_free(g_allocHeap, comp->preferredId);

  if (comp->localeCount) {
    for (u32 i = 0; i != comp->localeCount; ++i) {
      string_maybe_free(g_allocHeap, comp->localeNames[i]);
    }
    alloc_free_array_t(g_allocHeap, comp->localeEntries, comp->localeCount);
    alloc_free_array_t(g_allocHeap, comp->localeNames, comp->localeCount);
  }
}

static void loc_entries_init(EcsWorld* world, LocManagerComp* man, AssetManagerComp* assets) {
  const String assetPattern = string_lit("locale/*.locale");
  EcsEntityId  assetEntities[asset_query_max_results];
  const u32    assetCount = asset_query(world, assets, assetPattern, assetEntities);
  if (assetCount) {
    man->localeCount   = assetCount;
    man->localeEntries = alloc_array_t(g_allocHeap, LocManagerEntry, assetCount);
    man->localeNames   = alloc_array_t(g_allocHeap, String, assetCount);

    for (u32 i = 0; i != assetCount; ++i) {
      asset_acquire(world, assetEntities[i]);
      man->localeEntries[i] = (LocManagerEntry){
          .asset = assetEntities[i],
          .flags = LocManagerEntry_Acquired,
      };
      man->localeNames[i] = string_empty;
    }
  }
}

static bool loc_entries_load(EcsWorld* world, LocManagerComp* man, EcsIterator* assetItr) {
  bool ready = true;
  for (u32 i = 0; i != man->localeCount; ++i) {
    LocManagerEntry* entry = &man->localeEntries[i];
    ecs_view_jump(assetItr, entry->asset);

    if (entry->flags & (LocManagerEntry_Initialized | LocManagerEntry_Failed)) {
      continue; // Already initialized.
    }

    const String assetId = asset_id(ecs_view_read_t(assetItr, AssetComp));
    if (ecs_world_has_t(world, entry->asset, AssetFailedComp)) {
      log_e("Failed to load locale asset", log_param("id", fmt_text(assetId)));
      goto Failed;
    }
    if (!ecs_world_has_t(world, entry->asset, AssetLoadedComp)) {
      ready = false;
      continue; // Still loading.
    }
    const AssetLocaleComp* localeComp = ecs_view_read_t(assetItr, AssetLocaleComp);
    if (!localeComp) {
      log_e("Invalid locale asset", log_param("id", fmt_text(assetId)));
      goto Failed;
    }

    man->localeNames[i] = string_dup(g_allocHeap, localeComp->name);

    entry->flags |= LocManagerEntry_Initialized;
    if (localeComp->isDefault) {
      entry->flags |= LocManagerEntry_Default;
    }
    entry->id = path_stem(assetId);
    continue;

  Failed:
    entry->flags |= LocManagerEntry_Failed;
    man->localeNames[i] = string_dup(g_allocHeap, string_lit("Error"));
  }
  return ready;
}

static u32 loc_entries_default(const LocManagerComp* man) {
  for (u32 i = 0; i != man->localeCount; ++i) {
    const LocManagerEntryFlags reqFlags = LocManagerEntry_Initialized | LocManagerEntry_Default;
    if ((man->localeEntries[i].flags & reqFlags) == reqFlags) {
      return i;
    }
  }
  for (u32 i = 0; i != man->localeCount; ++i) {
    if (man->localeEntries[i].flags & LocManagerEntry_Initialized) {
      return i;
    }
  }
  return sentinel_u32;
}

static u32 loc_entries_pick(const LocManagerComp* man, const String preferredLocale) {
  if (!string_is_empty(preferredLocale)) {
    for (u32 i = 0; i != man->localeCount; ++i) {
      const LocManagerEntry* entry = &man->localeEntries[i];
      if (!(entry->flags & LocManagerEntry_Initialized)) {
        continue; // Failed to load.
      }
      if (string_match_glob(entry->id, preferredLocale, StringMatchFlags_IgnoreCase)) {
        return i;
      }
    }
  }
  return loc_entries_default(man);
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(LocManagerComp);
}

ecs_view_define(LocaleAssetView) {
  ecs_access_read(AssetComp);
  ecs_access_maybe_read(AssetLocaleComp);
}

ecs_system_define(LocUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  LocManagerComp*   man    = ecs_view_write_t(globalItr, LocManagerComp);
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  EcsIterator* assetItr = ecs_view_itr(ecs_world_view_t(world, LocaleAssetView));

  switch (man->state) {
  case LocManagerState_Init:
    loc_entries_init(world, man, assets);
    man->state = LocManagerState_Loading;
    break;
  case LocManagerState_Loading:
    if (loc_entries_load(world, man, assetItr)) {
      man->state        = LocManagerState_Ready;
      man->localeActive = loc_entries_pick(man, man->preferredId);
      if (!sentinel_check(man->localeActive)) {
        const LocManagerEntry* entry = &man->localeEntries[man->localeActive];
        log_i("Locale selected", log_param("id", fmt_text(entry->id)));
      }
    }
    break;
  case LocManagerState_Ready:
    for (u32 i = 0; i != man->localeCount; ++i) {
      LocManagerEntry* entry = &man->localeEntries[i];

      const bool shouldLoad  = i == man->localeActive;
      const bool isAcquired  = (entry->flags & LocManagerEntry_Acquired) != 0;
      const bool isUnloading = (entry->flags & LocManagerEntry_Unloading) != 0;
      const bool isLoaded    = ecs_world_has_t(world, entry->asset, AssetLoadedComp);
      const bool isFailed    = ecs_world_has_t(world, entry->asset, AssetFailedComp);
      const bool hasChanged  = ecs_world_has_t(world, entry->asset, AssetChangedComp);

      if (shouldLoad && !isAcquired && !isUnloading) {
        asset_acquire(world, entry->asset);
        entry->flags |= LocManagerEntry_Acquired;
        continue;
      }
      if (isAcquired && !shouldLoad) {
        asset_release(world, entry->asset);
        loc_translate_source_unset(entry->asset);
        entry->flags &= ~LocManagerEntry_Acquired;
        continue;
      }
      if (isAcquired && hasChanged && (isLoaded || isFailed)) {
        asset_release(world, entry->asset);
        loc_translate_source_unset(entry->asset);
        entry->flags &= ~LocManagerEntry_Acquired;
        entry->flags |= LocManagerEntry_Unloading;
        continue;
      }
      if (isUnloading && !(isLoaded || isFailed)) {
        entry->flags &= ~LocManagerEntry_Unloading; // Unload finished.
        continue;
      }
      ecs_view_jump(assetItr, entry->asset);
      const AssetLocaleComp* localeComp = ecs_view_read_t(assetItr, AssetLocaleComp);
      if (shouldLoad && isAcquired && localeComp) {
        loc_translate_source_set(entry->asset, localeComp);
      } else {
        loc_translate_source_unset(entry->asset);
      }
    }
    break;
  }
}

ecs_module_init(loc_manager_module) {
  ecs_register_comp(LocManagerComp, .destructor = ecs_destruct_loc_manager);

  ecs_register_view(UpdateGlobalView);
  ecs_register_view(LocaleAssetView);

  ecs_register_system(LocUpdateSys, ecs_view_id(UpdateGlobalView), ecs_view_id(LocaleAssetView));
}

LocManagerComp* loc_manager_init(EcsWorld* world, const String preferredId) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      LocManagerComp,
      .preferredId  = string_maybe_dup(g_allocHeap, preferredId),
      .localeActive = sentinel_u32);
}

bool loc_manager_ready(const LocManagerComp* man) { return man->state == LocManagerState_Ready; }

const String* loc_manager_locale_names(const LocManagerComp* man) {
  return man->state == LocManagerState_Ready ? man->localeNames : null;
}

u32 loc_manager_locale_count(const LocManagerComp* man) {
  return man->state == LocManagerState_Ready ? man->localeCount : 0;
}

u32 loc_manager_active_get(const LocManagerComp* man) { return man->localeActive; }

String loc_manager_active_id(const LocManagerComp* man) {
  if (sentinel_check(man->localeActive)) {
    return string_empty;
  }
  return man->localeEntries[man->localeActive].id;
}

void loc_manager_active_set(LocManagerComp* man, const u32 localeIndex) {
  diag_assert(localeIndex < man->localeCount);
  if (man->localeActive == localeIndex) {
    return;
  }
  const LocManagerEntry* entry = &man->localeEntries[localeIndex];
  if (entry->flags & LocManagerEntry_Initialized) {
    man->localeActive = localeIndex;
    log_i("Locale selected", log_param("id", fmt_text(entry->id)));
  } else {
    man->localeActive = sentinel_u32;
  }
}
