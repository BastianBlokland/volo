#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_renderable.h"

typedef enum {
  Terrain_HeightmapAcquired  = 1 << 0,
  Terrain_HeightmapUnloading = 1 << 1,
} TerrainFlags;

ecs_comp_define(SceneTerrainComp) {
  TerrainFlags flags;
  String       heightmapId;
  EcsEntityId  heightmapEntity;
};

static void ecs_destruct_terrain(void* data) {
  SceneTerrainComp* comp = data;
  string_free(g_alloc_heap, comp->heightmapId);
}

ecs_view_define(GlobalLoadView) {
  ecs_access_write(SceneTerrainComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(GlobalUnloadView) { ecs_access_write(SceneTerrainComp); }

ecs_system_define(SceneTerrainLoadSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalLoadView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneTerrainComp* terrain = ecs_view_write_t(globalItr, SceneTerrainComp);
  AssetManagerComp* assets  = ecs_view_write_t(globalItr, AssetManagerComp);

  if (!terrain->heightmapEntity) {
    terrain->heightmapEntity = asset_lookup(world, assets, terrain->heightmapId);
  }

  if (!(terrain->flags & (Terrain_HeightmapAcquired | Terrain_HeightmapUnloading))) {
    log_i("Acquiring terrain heightmap", log_param("id", fmt_text(terrain->heightmapId)));
    asset_acquire(world, terrain->heightmapEntity);
    terrain->flags |= Terrain_HeightmapAcquired;
  }
}

ecs_system_define(SceneTerrainUnloadSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUnloadView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneTerrainComp* terrain = ecs_view_write_t(globalItr, SceneTerrainComp);
  if (!terrain->heightmapEntity) {
    return; // Heightmap entity not yet looked up.
  }

  const bool isLoaded   = ecs_world_has_t(world, terrain->heightmapEntity, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, terrain->heightmapEntity, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, terrain->heightmapEntity, AssetChangedComp);

  if (terrain->flags & Terrain_HeightmapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading terrain heightmap",
        log_param("id", fmt_text(terrain->heightmapId)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, terrain->heightmapEntity);
    terrain->flags &= ~Terrain_HeightmapAcquired;
    terrain->flags |= Terrain_HeightmapUnloading;
  }
  if (terrain->flags & Terrain_HeightmapUnloading && !isLoaded) {
    terrain->flags &= ~Terrain_HeightmapUnloading;
  }
}

ecs_module_init(scene_terrain_module) {
  ecs_register_comp(SceneTerrainComp, .destructor = ecs_destruct_terrain);

  ecs_register_view(GlobalLoadView);
  ecs_register_view(GlobalUnloadView);

  ecs_register_system(SceneTerrainLoadSys, ecs_view_id(GlobalLoadView));
  ecs_register_system(SceneTerrainUnloadSys, ecs_view_id(GlobalUnloadView));
}

void scene_terrain_init(EcsWorld* world, const String heightmapId) {
  diag_assert_msg(heightmapId.size, "Invalid terrain heightmapId");

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneTerrainComp,
      .heightmapId = string_dup(g_alloc_heap, heightmapId));
}
