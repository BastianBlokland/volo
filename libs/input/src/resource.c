#include "asset/manager.h"
#include "core/alloc.h"
#include "core/diag.h"
#include "ecs/entity.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "log/logger.h"

#include "resource_internal.h"

typedef enum {
  InputResMap_Acquired  = 1 << 0,
  InputResMap_Unloading = 1 << 1,
} InputResMapFlags;

typedef struct {
  InputResMapFlags flags;
  String           id;
  EcsEntityId      asset;
} InputResMap;

ecs_comp_define(InputResourceComp) { InputResMap maps[input_resource_max_maps]; };

static void ecs_destruct_input_resource(void* data) {
  InputResourceComp* comp = data;
  for (u32 i = 0; i != input_resource_max_maps; ++i) {
    string_maybe_free(g_allocHeap, comp->maps[i].id);
  }
}

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GlobalResourceView) { ecs_access_write(InputResourceComp); }

static AssetManagerComp* input_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static InputResourceComp* input_resource(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, InputResourceComp) : null;
}

ecs_system_define(InputResourceUpdateSys) {
  AssetManagerComp*  assets   = input_asset_manager(world);
  InputResourceComp* resource = input_resource(world);
  if (!assets || !resource) {
    return;
  }
  for (u32 i = 0; i != input_resource_max_maps; ++i) {
    InputResMap* map = &resource->maps[i];
    if (string_is_empty(map->id)) {
      continue;
    }
    if (!map->asset) {
      map->asset = asset_lookup(world, assets, map->id);
    }
    const bool isLoaded   = ecs_world_has_t(world, map->asset, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, map->asset, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, map->asset, AssetChangedComp);

    if (isFailed) {
      log_e("Failed to load input-map", log_param("id", fmt_text(map->id)));
    }
    if (!(map->flags & (InputResMap_Acquired | InputResMap_Unloading))) {
      log_i("Acquiring input-map", log_param("id", fmt_text(map->id)));
      asset_acquire(world, map->asset);
      map->flags |= InputResMap_Acquired;
    }
    if (map->flags & InputResMap_Acquired && (isLoaded || isFailed) && hasChanged) {
      asset_release(world, map->asset);
      map->flags &= ~InputResMap_Acquired;
      map->flags |= InputResMap_Unloading;
    }
    if (map->flags & InputResMap_Unloading && !(isLoaded || isFailed)) {
      map->flags &= ~InputResMap_Unloading; // Unload finished.
    }
  }
}

ecs_module_init(input_resource_module) {
  ecs_register_comp(InputResourceComp, .destructor = ecs_destruct_input_resource);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalResourceView);

  ecs_register_system(
      InputResourceUpdateSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GlobalResourceView));
}

InputResourceComp* input_resource_init(EcsWorld* world) {
  return ecs_world_add_t(world, ecs_world_global(world), InputResourceComp);
}

void input_resource_load_map(InputResourceComp* resource, const String inputMapId) {
  diag_assert_msg(inputMapId.size, "Invalid inputMapId");

  for (u32 i = 0; i != input_resource_max_maps; ++i) {
    InputResMap* map = &resource->maps[i];
    if (!string_is_empty(map->id)) {
      continue; // Slot already in use.
    }
    *map = (InputResMap){
        .id = string_dup(g_allocHeap, inputMapId),
    };
    return;
  }
  diag_crash_msg("Loaded input map count exceeds maximum ({})", fmt_int(input_resource_max_maps));
}

u32 input_resource_maps(
    const InputResourceComp* resource, EcsEntityId out[PARAM_ARRAY_SIZE(input_resource_max_maps)]) {
  u32 count = 0;
  for (u32 i = 0; i != input_resource_max_maps; ++i) {
    const InputResMap* map = &resource->maps[i];
    if (ecs_entity_valid(map->asset)) {
      out[count++] = map->asset;
    }
  }
  return count;
}
