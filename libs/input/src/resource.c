#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

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

ecs_comp_define(InputResourceComp) {
  InputResMap maps[input_resource_max_maps];
  u32         mapCount;
};

static void ecs_destruct_input_resource(void* data) {
  InputResourceComp* comp = data;
  for (u32 i = 0; i != comp->mapCount; ++i) {
    string_free(g_alloc_heap, comp->maps[i].id);
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

ecs_system_define(InputResourceInitSys) {
  AssetManagerComp*  assets   = input_asset_manager(world);
  InputResourceComp* resource = input_resource(world);
  if (!assets || !resource) {
    return;
  }
  for (u32 i = 0; i != resource->mapCount; ++i) {
    InputResMap* map = &resource->maps[i];
    if (!map->asset) {
      map->asset = asset_lookup(world, assets, map->id);
    }
    if (!(map->flags & (InputResMap_Acquired | InputResMap_Unloading))) {
      log_i("Acquiring input-map", log_param("id", fmt_text(map->id)));
      asset_acquire(world, map->asset);
      map->flags |= InputResMap_Acquired;
    }
  }
}

ecs_system_define(InputResourceUnloadChangedMapSys) {
  InputResourceComp* resource = input_resource(world);
  if (!resource) {
    return;
  }
  for (u32 i = 0; i != resource->mapCount; ++i) {
    InputResMap* map = &resource->maps[i];
    if (!ecs_entity_valid(map->asset)) {
      continue;
    }
    const bool isLoaded   = ecs_world_has_t(world, map->asset, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, map->asset, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, map->asset, AssetChangedComp);

    if (map->flags & InputResMap_Acquired && (isLoaded || isFailed) && hasChanged) {
      log_i(
          "Unloading input-map",
          log_param("id", fmt_text(map->id)),
          log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, map->asset);
      map->flags &= ~InputResMap_Acquired;
      map->flags |= InputResMap_Unloading;
    }
    if (map->flags & InputResMap_Unloading && !isLoaded) {
      map->flags &= ~InputResMap_Unloading;
    }
  }
}

ecs_module_init(input_resource_module) {
  ecs_register_comp(InputResourceComp, .destructor = ecs_destruct_input_resource);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalResourceView);

  ecs_register_system(
      InputResourceInitSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GlobalResourceView));
  ecs_register_system(InputResourceUnloadChangedMapSys, ecs_view_id(GlobalResourceView));
}

void input_resource_init(EcsWorld* world, const String inputMapId) {
  diag_assert_msg(inputMapId.size, "Invalid inputMapId");

  const InputResMap map = {
      .id = string_dup(g_alloc_heap, inputMapId),
  };

  ecs_world_add_t(world, ecs_world_global(world), InputResourceComp, .maps[0] = map, .mapCount = 1);
}

EcsEntityId input_resource_map(const InputResourceComp* comp) { return comp->maps[0].asset; }
