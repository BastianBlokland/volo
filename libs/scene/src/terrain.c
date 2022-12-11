#include "asset_manager.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_renderable.h"

typedef enum {
  Terrain_HeightmapAcquired    = 1 << 0,
  Terrain_HeightmapUnloading   = 1 << 1,
  Terrain_HeightmapUnsupported = 1 << 2,
} TerrainFlags;

ecs_comp_define(SceneTerrainComp) {
  TerrainFlags     flags;
  String           heightmapId;
  EcsEntityId      heightmapEntity;
  Mem              heightmapData;
  u32              heightmapSize;
  AssetTextureType heightmapType;
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

ecs_view_define(TextureReadView) { ecs_access_read(AssetTextureComp); }

static void terrain_heightmap_unsupported(SceneTerrainComp* terrain, const String error) {
  log_e(
      "Unsupported terrain heightmap",
      log_param("error", fmt_text(error)),
      log_param("id", fmt_text(terrain->heightmapId)));

  terrain->flags |= Terrain_HeightmapUnsupported;
}

static void terrain_heightmap_load(SceneTerrainComp* terrain, const AssetTextureComp* tex) {
  diag_assert_msg(!terrain->heightmapData.size, "Heightmap already loaded");

  if (tex->flags & AssetTextureFlags_Srgb) {
    terrain_heightmap_unsupported(terrain, string_lit("Srgb"));
    return;
  }
  if (tex->channels != AssetTextureChannels_One) {
    terrain_heightmap_unsupported(terrain, string_lit("More then one channel"));
    return;
  }
  if (tex->width != tex->height) {
    terrain_heightmap_unsupported(terrain, string_lit("Not square"));
    return;
  }
  terrain->heightmapData = asset_texture_data(tex);
  terrain->heightmapSize = tex->width;
  terrain->heightmapType = tex->type;

  log_d(
      "Terrain heightmap loaded",
      log_param("id", fmt_text(terrain->heightmapId)),
      log_param("type", fmt_text(asset_texture_type_str(terrain->heightmapType))),
      log_param("size", fmt_int(terrain->heightmapSize)));
}

static void terrain_heightmap_unload(SceneTerrainComp* terrain) {
  terrain->heightmapData = mem_empty;
  terrain->heightmapSize = 0;
  terrain->flags &= ~Terrain_HeightmapUnsupported;

  log_d("Terrain heightmap unloaded", log_param("id", fmt_text(terrain->heightmapId)));
}

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

  enum { LoadBlockerFlags = Terrain_HeightmapUnloading | Terrain_HeightmapUnsupported };
  const bool loadBlocked = (terrain->flags & LoadBlockerFlags) != 0;

  if (terrain->heightmapData.size == 0 && !loadBlocked) {
    EcsView*     textureView = ecs_world_view_t(world, TextureReadView);
    EcsIterator* textureItr  = ecs_view_maybe_at(textureView, terrain->heightmapEntity);
    if (textureItr) {
      terrain_heightmap_load(terrain, ecs_view_read_t(textureItr, AssetTextureComp));
    }
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
    terrain_heightmap_unload(terrain);

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
  ecs_register_view(TextureReadView);

  ecs_register_system(
      SceneTerrainLoadSys, ecs_view_id(GlobalLoadView), ecs_view_id(TextureReadView));
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
