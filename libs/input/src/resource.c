#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "input_resource.h"
#include "log_logger.h"

typedef enum {
  InputRes_MapAcquired  = 1 << 0,
  InputRes_MapUnloading = 1 << 1,
} InputResFlags;

ecs_comp_define(InputResourceComp) {
  InputResFlags flags;
  String        mapId;
  EcsEntityId   mapEntity;
};

static void ecs_destruct_input_resource(void* data) {
  InputResourceComp* comp = data;
  string_free(g_alloc_heap, comp->mapId);
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

  if (!resource->mapEntity) {
    resource->mapEntity = asset_lookup(world, assets, resource->mapId);
  }

  if (!(resource->flags & (InputRes_MapAcquired | InputRes_MapUnloading))) {
    log_i("Acquiring inputmap", log_param("id", fmt_text(resource->mapId)));
    asset_acquire(world, resource->mapEntity);
    resource->flags |= InputRes_MapAcquired;
  }
}

ecs_system_define(InputResourceUnloadChangedMapSys) {
  InputResourceComp* resource = input_resource(world);
  if (!resource || !ecs_entity_valid(resource->mapEntity)) {
    return;
  }
  const bool isLoaded   = ecs_world_has_t(world, resource->mapEntity, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, resource->mapEntity, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, resource->mapEntity, AssetChangedComp);

  if (resource->flags & InputRes_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading inputmap",
        log_param("id", fmt_text(resource->mapId)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, resource->mapEntity);
    resource->flags &= ~InputRes_MapAcquired;
    resource->flags |= InputRes_MapUnloading;
  }
  if (resource->flags & InputRes_MapUnloading && !isLoaded) {
    resource->flags &= ~InputRes_MapUnloading;
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

void input_resource_create(EcsWorld* world, const String inputMapId) {
  diag_assert_msg(inputMapId.size, "Invalid inputMapId");

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      InputResourceComp,
      .mapId = string_dup(g_alloc_heap, inputMapId));
}

EcsEntityId input_resource_map(const InputResourceComp* comp) { return comp->mapEntity; }
