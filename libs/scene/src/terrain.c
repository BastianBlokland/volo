#include "asset_manager.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "geo_plane.h"
#include "log_logger.h"
#include "scene_terrain.h"

#define scene_terrain_size_axis 400.0f

static const f32 g_terrainSize        = scene_terrain_size_axis;
static const f32 g_terrainSizeHalf    = scene_terrain_size_axis * 0.5f;
static const f32 g_terrainSizeInv     = 1.0f / scene_terrain_size_axis;
static const f32 g_terrainHeightScale = 3.0f;

typedef enum {
  Terrain_HeightmapAcquired    = 1 << 0,
  Terrain_HeightmapUnloading   = 1 << 1,
  Terrain_HeightmapUnsupported = 1 << 2,
} TerrainFlags;

ecs_comp_define(SceneTerrainComp) {
  TerrainFlags flags;
  u32          version;

  String      graphicId;
  EcsEntityId graphicEntity;

  String           heightmapId;
  EcsEntityId      heightmapEntity;
  Mem              heightmapData;
  u32              heightmapSize;
  AssetTextureType heightmapType;
};

ecs_view_define(GlobalLoadView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_write(SceneTerrainComp);
}

ecs_view_define(GlobalUnloadView) { ecs_access_write(SceneTerrainComp); }

ecs_view_define(TextureReadView) {
  ecs_access_read(AssetTextureComp);
  ecs_access_with(AssetLoadedComp);
  ecs_access_without(AssetChangedComp);
}

static void terrain_heightmap_unsupported(SceneTerrainComp* terrain, const String error) {
  log_e(
      "Unsupported terrain heightmap",
      log_param("error", fmt_text(error)),
      log_param("id", fmt_text(terrain->heightmapId)));

  terrain->flags |= Terrain_HeightmapUnsupported;
}

static bool terrain_heightmap_load(SceneTerrainComp* terrain, const AssetTextureComp* tex) {
  diag_assert_msg(!terrain->heightmapData.size, "Heightmap already loaded");

  if (tex->flags & AssetTextureFlags_Srgb) {
    terrain_heightmap_unsupported(terrain, string_lit("Srgb"));
    return false;
  }
  if (tex->channels != AssetTextureChannels_One) {
    terrain_heightmap_unsupported(terrain, string_lit("More then one channel"));
    return false;
  }
  if (tex->width != tex->height) {
    terrain_heightmap_unsupported(terrain, string_lit("Not square"));
    return false;
  }
  if (tex->layers > 1) {
    terrain_heightmap_unsupported(terrain, string_lit("Layer count greater than 1"));
    return false;
  }
  if (tex->type != AssetTextureType_U16) {
    // TODO: Support other types.
    terrain_heightmap_unsupported(terrain, string_lit("Non-u16 format"));
    return false;
  }
  ++terrain->version;
  terrain->heightmapData = asset_texture_data(tex);
  terrain->heightmapSize = tex->width;
  terrain->heightmapType = tex->type;

  log_d(
      "Terrain heightmap loaded",
      log_param("id", fmt_text(terrain->heightmapId)),
      log_param("type", fmt_text(asset_texture_type_str(terrain->heightmapType))),
      log_param("size", fmt_int(terrain->heightmapSize)));

  return true;
}

static void terrain_heightmap_unload(SceneTerrainComp* terrain) {
  ++terrain->version;
  terrain->flags &= ~Terrain_HeightmapUnsupported;
  terrain->heightmapData = mem_empty;
  terrain->heightmapSize = 0;

  log_d("Terrain heightmap unloaded", log_param("id", fmt_text(terrain->heightmapId)));
}

/**
 * Sample the heightmap at the given coordinate.
 * NOTE: Returns a normalized (0 - 1) float.
 */
static f32 terrain_heightmap_sample(const SceneTerrainComp* t, const f32 xNorm, const f32 yNorm) {
  if (UNLIKELY(xNorm < 0 || xNorm > 1 || yNorm < 0 || yNorm > 1)) {
    return 0.0f;
  }
  if (UNLIKELY(!t->heightmapData.size)) {
    return 0.0f; // No heightmap loaded at the moment.
  }
  diag_assert(t->heightmapType == AssetTextureType_U16);

  static const f32 g_normMul = 1.0f / u16_max;

  const u16* pixels = t->heightmapData.ptr;
  const f32  x = xNorm * (t->heightmapSize - 1), y = yNorm * (t->heightmapSize - 1);

  /**
   * Bi-linearly interpolate 4 pixels around the required coordinate.
   */
  const f32 corner1x = math_min(t->heightmapSize - 2, math_round_down_f32(x));
  const f32 corner1y = math_min(t->heightmapSize - 2, math_round_down_f32(y));
  const f32 corner2x = corner1x + 1.0f, corner2y = corner1y + 1.0f;

  const f32 p1 = pixels[(usize)corner1y * t->heightmapSize + (usize)corner1x] * g_normMul;
  const f32 p2 = pixels[(usize)corner1y * t->heightmapSize + (usize)corner2x] * g_normMul;
  const f32 p3 = pixels[(usize)corner2y * t->heightmapSize + (usize)corner1x] * g_normMul;
  const f32 p4 = pixels[(usize)corner2y * t->heightmapSize + (usize)corner2x] * g_normMul;

  const f32 tX = x - corner1x, tY = y - corner1y;
  return math_lerp(math_lerp(p1, p2, tX), math_lerp(p3, p4, tX), tY);
}

ecs_system_define(SceneTerrainLoadSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalLoadView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneTerrainComp* terrain = ecs_view_write_t(globalItr, SceneTerrainComp);
  if (!terrain) {
    terrain              = ecs_world_add_t(world, ecs_world_global(world), SceneTerrainComp);
    terrain->graphicId   = string_lit("graphics/scene/terrain.graphic");
    terrain->heightmapId = string_lit("external/terrain/terrain_3_height.r16");
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  if (!terrain->graphicEntity) {
    terrain->graphicEntity = asset_lookup(world, assets, terrain->graphicId);
  }
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
  ecs_register_comp(SceneTerrainComp);

  ecs_register_view(GlobalLoadView);
  ecs_register_view(GlobalUnloadView);
  ecs_register_view(TextureReadView);

  ecs_register_system(
      SceneTerrainLoadSys, ecs_view_id(GlobalLoadView), ecs_view_id(TextureReadView));
  ecs_register_system(SceneTerrainUnloadSys, ecs_view_id(GlobalUnloadView));
}

bool scene_terrain_loaded(const SceneTerrainComp* terrain) {
  return terrain->heightmapData.size != 0;
}

u32 scene_terrain_version(const SceneTerrainComp* terrain) { return terrain->version; }

EcsEntityId scene_terrain_graphic(const SceneTerrainComp* terrain) {
  return terrain->graphicEntity;
}

f32 scene_terrain_size(const SceneTerrainComp* terrain) {
  (void)terrain;
  return g_terrainSize;
}

f32 scene_terrain_height_scale(const SceneTerrainComp* terrain) {
  (void)terrain;
  return g_terrainHeightScale;
}

GeoBox scene_terrain_bounds(const SceneTerrainComp* terrain) {
  (void)terrain;
  return (GeoBox){
      .min = geo_vector(-g_terrainSizeHalf, 0, -g_terrainSizeHalf),
      .max = geo_vector(g_terrainSizeHalf, g_terrainHeightScale, g_terrainSizeHalf),
  };
}

f32 scene_terrain_intersect_ray(
    const SceneTerrainComp* terrain, const GeoRay* ray, const f32 maxDist) {
  /**
   * Approximate the terrain intersection by ray-marching the heightmap.
   * Performs a binary-search through the ray until we've found a height that is close enough or
   * we've hit the begin/end.
   *
   * TODO: This does not support cases where the ray crosses the terrain multiple times, for example
   * a ray that enters and exits a hill in the terrain might 'hit' either side at the moment.
   */
  const GeoPlane planeZero  = {.normal = geo_up};
  const f32      planeZeroT = geo_plane_intersect_ray(&planeZero, ray);
  if (planeZeroT < 0) {
    return -1.0f;
  }
  static const f32 g_heightThreshold = 0.05f;
  f32              tMin = 0.0f, tMax = math_min(planeZeroT, maxDist);
  while (tMin < tMax) {
    const f32       tPos          = tMin + (tMax - tMin) * 0.5f; // Middle point of the search-area.
    const GeoVector rayPos        = geo_ray_position(ray, tPos);
    const f32       terrainHeight = scene_terrain_height(terrain, rayPos);
    const f32       heightDiff    = terrainHeight - rayPos.y;
    if (math_abs(heightDiff) <= g_heightThreshold) {
      return tPos;
    }
    if (heightDiff > 0) {
      tMax = tPos;
    } else {
      tMin = tPos + g_heightThreshold;
    }
  }
  return -1.0f;
}

GeoVector scene_terrain_normal(const SceneTerrainComp* terrain, const GeoVector position) {
  if (UNLIKELY(!terrain->heightmapData.size)) {
    return geo_up; // No heightmap loaded at the moment.
  }
  static const f32 g_normMul = 1.0f / u16_max;
  const u16*       pixels    = terrain->heightmapData.ptr;

  /**
   * Compute the normal by sampling 2 height's around the given position on both x and z axis.
   * NOTE: Does not interpolate so the normal is not continuous over the terrain surface.
   */

  const f32 normX = (position.x + g_terrainSizeHalf) * g_terrainSizeInv;
  const f32 normY = (position.z + g_terrainSizeHalf) * g_terrainSizeInv;

  const i32 size = terrain->heightmapSize;
  const i32 x    = (i32)math_round_nearest_f32(normX * (size - 1));
  const i32 y    = (i32)math_round_nearest_f32(normY * (size - 1));

  if (UNLIKELY(x < 0 || x >= size || y < 0 || y >= size)) {
    return geo_up; // Position is outside of the heightmap.
  }

  const i32 x0 = LIKELY(x > 0) ? x - 1 : x;
  const i32 x1 = LIKELY(x < size - 1) ? x + 1 : x;
  f32       dX = pixels[y * size + x0] * g_normMul - pixels[y * size + x1] * g_normMul;
  if (UNLIKELY(x == 0 || x == size - 1)) {
    dX *= 2.0f;
  }

  const i32 y0 = LIKELY(y > 0) ? y - 1 : y;
  const i32 y1 = LIKELY(y < size - 1) ? y + 1 : y;
  f32       dY = pixels[y0 * size + x] * g_normMul - pixels[y1 * size + x] * g_normMul;
  if (UNLIKELY(y == 0 || y == size - 1)) {
    dY *= 2.0f;
  }

  const f32       xzScale = g_terrainSize / size;
  const GeoVector dir     = {dX * g_terrainHeightScale, xzScale * 2, dY * g_terrainHeightScale};
  return geo_vector_norm(dir);
}

f32 scene_terrain_height(const SceneTerrainComp* terrain, const GeoVector position) {
  const f32 heightmapX = (position.x + g_terrainSizeHalf) * g_terrainSizeInv;
  const f32 heightmapY = (position.z + g_terrainSizeHalf) * g_terrainSizeInv;
  return terrain_heightmap_sample(terrain, heightmapX, heightmapY) * g_terrainHeightScale;
}

void scene_terrain_snap(const SceneTerrainComp* terrain, GeoVector* position) {
  position->y = scene_terrain_height(terrain, *position);
}
