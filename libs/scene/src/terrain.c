#include "asset_manager.h"
#include "asset_terrain.h"
#include "asset_texture.h"
#include "core_diag.h"
#include "core_intrinsic.h"
#include "core_math.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "geo_box.h"
#include "geo_plane.h"
#include "log_logger.h"
#include "scene_level.h"
#include "scene_terrain.h"

typedef enum {
  TerrainState_Idle,
  TerrainState_AssetLoad,
  TerrainState_HeightmapLoad,
  TerrainState_Loaded,
  TerrainState_Error,
} TerrainState;

ecs_comp_define(SceneTerrainComp) {
  TerrainState state : 8;
  bool         updated : 8;
  u32          version;

  EcsEntityId terrainAsset;
  EcsEntityId graphicAsset;

  EcsEntityId        heightmapAsset;
  Mem                heightmapData;
  u32                heightmapSize;
  AssetTextureFormat heightmapFormat;

  f32 size, sizeHalf, sizeInv;
  f32 playSize, playSizeHalf;
  f32 heightMax;

  GeoColor minimapColorLow, minimapColorHigh;
};

ecs_view_define(GlobalLoadView) {
  ecs_access_maybe_write(SceneTerrainComp);
  ecs_access_read(SceneLevelManagerComp);
}

ecs_view_define(AssetTerrainReadView) {
  ecs_access_read(AssetTerrainComp);
  ecs_access_with(AssetLoadedComp);
  ecs_access_without(AssetChangedComp);
}

ecs_view_define(AssetTextureReadView) {
  ecs_access_read(AssetTextureComp);
  ecs_access_with(AssetLoadedComp);
  ecs_access_without(AssetChangedComp);
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
  diag_assert(t->heightmapFormat == AssetTextureFormat_u16_r);

  static const f32 g_normMul = 1.0f / u16_max;

  const u16* pixels = t->heightmapData.ptr;
  const f32  x = xNorm * (t->heightmapSize - 1), y = yNorm * (t->heightmapSize - 1);

  /**
   * Bi-linearly interpolate 4 pixels around the required coordinate.
   */
  const f32 corner1x = math_min(t->heightmapSize - 2, intrinsic_round_down_f32(x));
  const f32 corner1y = math_min(t->heightmapSize - 2, intrinsic_round_down_f32(y));
  const f32 corner2x = corner1x + 1.0f, corner2y = corner1y + 1.0f;

  const f32 p1 = pixels[(usize)corner1y * t->heightmapSize + (usize)corner1x] * g_normMul;
  const f32 p2 = pixels[(usize)corner1y * t->heightmapSize + (usize)corner2x] * g_normMul;
  const f32 p3 = pixels[(usize)corner2y * t->heightmapSize + (usize)corner1x] * g_normMul;
  const f32 p4 = pixels[(usize)corner2y * t->heightmapSize + (usize)corner2x] * g_normMul;

  const f32 tX = x - corner1x, tY = y - corner1y;
  return math_lerp(math_lerp(p1, p2, tX), math_lerp(p3, p4, tX), tY);
}

typedef struct {
  EcsWorld*                    world;
  SceneTerrainComp*            terrain;
  const SceneLevelManagerComp* levelManager;
  EcsView*                     assetTerrainView;
  EcsView*                     assetTextureView;
} TerrainLoadContext;

typedef enum {
  TerrainLoadResult_Done,
  TerrainLoadResult_Busy,
  TerrainLoadResult_Error,
} TerrainLoadResult;

static TerrainLoadResult terrain_asset_load(TerrainLoadContext* ctx) {
  if (ecs_world_has_t(ctx->world, ctx->terrain->terrainAsset, AssetFailedComp)) {
    log_e("Failed to load terrain asset");
    return TerrainLoadResult_Error;
  }
  if (!ecs_world_has_t(ctx->world, ctx->terrain->terrainAsset, AssetLoadedComp)) {
    return TerrainLoadResult_Busy;
  }
  EcsIterator* assetItr = ecs_view_maybe_at(ctx->assetTerrainView, ctx->terrain->terrainAsset);
  if (!assetItr) {
    log_e("Invalid terrain asset");
    return TerrainLoadResult_Error;
  }
  const AssetTerrainComp* asset  = ecs_view_read_t(assetItr, AssetTerrainComp);
  ctx->terrain->graphicAsset     = asset->graphic;
  ctx->terrain->heightmapAsset   = asset->heightmap;
  ctx->terrain->size             = asset->size;
  ctx->terrain->sizeHalf         = asset->size * 0.5f;
  ctx->terrain->sizeInv          = 1.0f / asset->size;
  ctx->terrain->playSize         = (f32)asset->playSize;
  ctx->terrain->playSizeHalf     = (f32)asset->playSize * 0.5f;
  ctx->terrain->heightMax        = asset->heightMax;
  ctx->terrain->minimapColorLow  = geo_color_srgb_to_linear(asset->minimapColorLow);
  ctx->terrain->minimapColorHigh = geo_color_srgb_to_linear(asset->minimapColorHigh);

  return TerrainLoadResult_Done;
}

static TerrainLoadResult terrain_heightmap_load(TerrainLoadContext* ctx) {
  diag_assert_msg(!ctx->terrain->heightmapData.size, "Heightmap already loaded");

  if (ecs_world_has_t(ctx->world, ctx->terrain->heightmapAsset, AssetFailedComp)) {
    log_e("Failed to load heightmap");
    return TerrainLoadResult_Error;
  }
  if (!ecs_world_has_t(ctx->world, ctx->terrain->heightmapAsset, AssetLoadedComp)) {
    return TerrainLoadResult_Busy;
  }
  EcsIterator* texItr = ecs_view_maybe_at(ctx->assetTextureView, ctx->terrain->heightmapAsset);
  if (!texItr) {
    log_e("Invalid heightmap asset");
    return TerrainLoadResult_Error;
  }
  const AssetTextureComp* tex = ecs_view_read_t(texItr, AssetTextureComp);
  if (tex->flags & AssetTextureFlags_Srgb) {
    log_e("Unsupported heightmap", log_param("error", fmt_text_lit("Srgb")));
    return TerrainLoadResult_Error;
  }
  if (tex->format != AssetTextureFormat_u16_r) {
    log_e("Unsupported heightmap", log_param("error", fmt_text_lit("Non u16-r format")));
    return TerrainLoadResult_Error;
  }
  if (tex->width != tex->height) {
    log_e("Unsupported heightmap", log_param("error", fmt_text_lit("Not square")));
    return TerrainLoadResult_Error;
  }
  if (tex->layers > 1) {
    log_e("Unsupported heightmap", log_param("error", fmt_text_lit("Layer count greater than 1")));
    return TerrainLoadResult_Error;
  }
  ctx->terrain->heightmapData   = asset_texture_data(tex);
  ctx->terrain->heightmapSize   = tex->width;
  ctx->terrain->heightmapFormat = tex->format;

  log_d(
      "Terrain heightmap loaded",
      log_param("format", fmt_text(asset_texture_format_str(tex->format))),
      log_param("size", fmt_int(tex->width)));

  return TerrainLoadResult_Done;
}

static bool terrain_should_unload(TerrainLoadContext* ctx) {
  if (scene_level_loading(ctx->levelManager)) {
    // Delay terrain unload until level loading is done, this avoids reloading terrain when the next
    // level uses the same terrain.
    return false;
  }
  if (ctx->terrain->terrainAsset != scene_level_terrain(ctx->levelManager)) {
    return true;
  }
  if (ecs_world_has_t(ctx->world, ctx->terrain->terrainAsset, AssetChangedComp)) {
    return true;
  }
  const EcsEntityId heightmap = ctx->terrain->heightmapAsset;
  if (heightmap && ecs_world_has_t(ctx->world, heightmap, AssetChangedComp)) {
    return true;
  }
  return false;
}

static void terrain_unload(TerrainLoadContext* ctx) {
  ctx->terrain->terrainAsset   = 0;
  ctx->terrain->graphicAsset   = 0;
  ctx->terrain->heightmapAsset = 0;
  ctx->terrain->heightmapData  = mem_empty;
  ctx->terrain->heightmapSize  = 0;
  ctx->terrain->state          = TerrainState_Idle;
}

ecs_system_define(SceneTerrainLoadSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalLoadView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneLevelManagerComp* levelManager = ecs_view_read_t(globalItr, SceneLevelManagerComp);
  SceneTerrainComp*            terrain      = ecs_view_write_t(globalItr, SceneTerrainComp);
  if (terrain) {
    terrain->updated = false;
  } else {
    terrain = ecs_world_add_t(world, ecs_world_global(world), SceneTerrainComp);
  }

  TerrainLoadContext ctx = {
      .world            = world,
      .terrain          = terrain,
      .levelManager     = levelManager,
      .assetTerrainView = ecs_world_view_t(world, AssetTerrainReadView),
      .assetTextureView = ecs_world_view_t(world, AssetTextureReadView),
  };

  switch (terrain->state) {
  case TerrainState_Idle: {
    const EcsEntityId levelAsset = scene_level_terrain(levelManager);
    if (levelAsset) {
      terrain->terrainAsset = levelAsset;
      asset_acquire(world, levelAsset);
      ++terrain->state;
      log_d("Loading terrain");
    }
  } break;
  case TerrainState_AssetLoad:
    switch (terrain_asset_load(&ctx)) {
    case TerrainLoadResult_Done:
      asset_release(world, terrain->terrainAsset);
      asset_acquire(world, terrain->heightmapAsset);
      ++terrain->state;
      break;
    case TerrainLoadResult_Error:
      asset_release(world, terrain->terrainAsset);
      terrain->state = TerrainState_Error;
      break;
    case TerrainLoadResult_Busy:
      break;
    }
    break;
  case TerrainState_HeightmapLoad:
    switch (terrain_heightmap_load(&ctx)) {
    case TerrainLoadResult_Done:
      ++terrain->state;
      ++terrain->version;
      terrain->updated = true;
      log_i("Terrain loaded", log_param("version", fmt_int(terrain->version)));
      break;
    case TerrainLoadResult_Error:
      asset_release(world, terrain->heightmapAsset);
      terrain->state = TerrainState_Error;
      break;
    case TerrainLoadResult_Busy:
      break;
    }
    break;
  case TerrainState_Loaded:
    if (terrain_should_unload(&ctx)) {
      asset_release(world, terrain->heightmapAsset);
      terrain_unload(&ctx);

      /**
       * If there's no level terrain (meaning we will not immediately load another terrain), then
       * bump the version so that other systems can update their cached data. Otherwise it can wait
       * until we've loaded the next terrain.
       */
      if (!scene_level_terrain(ctx.levelManager)) {
        ++terrain->version;
        terrain->updated = true;
      }
    }
    break;
  case TerrainState_Error:
    if (terrain_should_unload(&ctx)) {
      terrain_unload(&ctx);
    }
    break;
  }
}

ecs_module_init(scene_terrain_module) {
  ecs_register_comp(SceneTerrainComp);

  ecs_register_view(GlobalLoadView);
  ecs_register_view(AssetTextureReadView);
  ecs_register_view(AssetTerrainReadView);

  ecs_register_system(
      SceneTerrainLoadSys,
      ecs_view_id(GlobalLoadView),
      ecs_view_id(AssetTextureReadView),
      ecs_view_id(AssetTerrainReadView));
}

bool scene_terrain_loaded(const SceneTerrainComp* terrain) {
  return terrain->state == TerrainState_Loaded;
}

u32  scene_terrain_version(const SceneTerrainComp* terrain) { return terrain->version; }
bool scene_terrain_updated(const SceneTerrainComp* terrain) { return terrain->updated; }

EcsEntityId scene_terrain_resource_asset(const SceneTerrainComp* terrain) {
  return terrain->terrainAsset;
}

EcsEntityId scene_terrain_resource_graphic(const SceneTerrainComp* terrain) {
  return terrain->graphicAsset;
}

EcsEntityId scene_terrain_resource_heightmap(const SceneTerrainComp* terrain) {
  return terrain->heightmapAsset;
}

GeoColor scene_terrain_minimap_color_low(const SceneTerrainComp* terrain) {
  return terrain->minimapColorLow;
}

GeoColor scene_terrain_minimap_color_high(const SceneTerrainComp* terrain) {
  return terrain->minimapColorHigh;
}

f32 scene_terrain_size(const SceneTerrainComp* terrain) { return terrain->size; }
f32 scene_terrain_play_size(const SceneTerrainComp* terrain) { return terrain->playSize; }
f32 scene_terrain_height_max(const SceneTerrainComp* terrain) { return terrain->heightMax; }

GeoBox scene_terrain_bounds(const SceneTerrainComp* terrain) {
  return (GeoBox){
      .min = geo_vector(-terrain->sizeHalf, 0, -terrain->sizeHalf),
      .max = geo_vector(terrain->sizeHalf, terrain->heightMax, terrain->sizeHalf),
  };
}

GeoBox scene_terrain_play_bounds(const SceneTerrainComp* terrain) {
  return (GeoBox){
      .min = geo_vector(-terrain->playSizeHalf, 0, -terrain->playSizeHalf),
      .max = geo_vector(terrain->playSizeHalf, terrain->heightMax, terrain->playSizeHalf),
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
  static const f32 g_searchEpsilon   = 0.001f;
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
      tMax = tPos - g_searchEpsilon;
    } else {
      tMin = tPos + g_searchEpsilon;
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

  const f32 normX = (position.x + terrain->sizeHalf) * terrain->sizeInv;
  const f32 normY = (position.z + terrain->sizeHalf) * terrain->sizeInv;

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

  const f32       xzScale = terrain->size / size;
  const GeoVector dir     = {dX * terrain->heightMax, xzScale * 2, dY * terrain->heightMax};
  return geo_vector_norm(dir);
}

f32 scene_terrain_height(const SceneTerrainComp* terrain, const GeoVector position) {
  const f32 heightmapX = (position.x + terrain->sizeHalf) * terrain->sizeInv;
  const f32 heightmapY = (position.z + terrain->sizeHalf) * terrain->sizeInv;
  return terrain_heightmap_sample(terrain, heightmapX, heightmapY) * terrain->heightMax;
}

void scene_terrain_snap(const SceneTerrainComp* terrain, GeoVector* position) {
  position->y = scene_terrain_height(terrain, *position);
}
