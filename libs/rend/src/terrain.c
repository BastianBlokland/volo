#include "ecs_utils.h"
#include "ecs_world.h"
#include "rend_draw.h"
#include "scene_terrain.h"

static const u32 g_terrainPatchCountAxis = 16;

typedef struct {
  ALIGNAS(16)
  f32 size;
  f32 heightScale;
  f32 patchScale;
} RendTerrainData;

ASSERT(sizeof(RendTerrainData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  f32 posX;
  f32 posZ;
  f32 texU;
  f32 texV;
} RendTerrainPatchData;

ASSERT(sizeof(RendTerrainPatchData) == 16, "Size needs to match the size defined in glsl");

ecs_comp_define(RendTerrainComp) {
  u32         terrainVersion;
  EcsEntityId drawEntity;
};

static EcsEntityId rend_terrain_draw_create(EcsWorld* world) {
  EcsEntityId         e     = ecs_world_entity_create(world);
  const RendDrawFlags flags = RendDrawFlags_Terrain | RendDrawFlags_NoAutoClear;
  rend_draw_create(world, e, flags);
  return e;
}

static void rend_terrain_draw_init(const SceneTerrainComp* terrain, RendDrawComp* draw) {
  const f32 size          = scene_terrain_size(terrain);
  const f32 halfSize      = size * 0.5f;
  const f32 patchScale    = 1.0f / g_terrainPatchCountAxis;
  const f32 patchSize     = size * patchScale;
  const f32 patchHalfSize = patchSize * 0.5f;
  const f32 heightScale   = scene_terrain_height_scale(terrain);

  // Set global terrain meta.
  rend_draw_set_graphic(draw, scene_terrain_graphic(terrain));
  *rend_draw_set_data_t(draw, RendTerrainData) = (RendTerrainData){
      .size        = size,
      .heightScale = scene_terrain_height_scale(terrain),
      .patchScale  = patchScale,
  };

  // Clear previously added instances.
  rend_draw_clear(draw);

  // Add patch instances.
  const SceneTags patchTags = SceneTags_Terrain;
  for (u32 x = 0; x != g_terrainPatchCountAxis; ++x) {
    for (u32 z = 0; z != g_terrainPatchCountAxis; ++z) {
      const RendTerrainPatchData patchData = {
          .posX = x * patchSize - halfSize + patchHalfSize,
          .posZ = z * patchSize - halfSize + patchHalfSize,
          .texU = x * patchScale,
          .texV = z * patchScale,
      };
      const GeoVector patchCenter = {.x = patchData.posX, .y = 0, .z = patchData.posZ};
      const GeoBox    patchBounds = {
          .min = geo_vector_sub(patchCenter, geo_vector(patchHalfSize, 0, patchHalfSize)),
          .max = geo_vector_add(patchCenter, geo_vector(patchHalfSize, heightScale, patchHalfSize)),
      };
      *rend_draw_add_instance_t(draw, RendTerrainPatchData, patchTags, patchBounds) = patchData;
    }
  }
}

ecs_view_define(GlobalView) {
  ecs_access_maybe_write(RendTerrainComp);
  ecs_access_read(SceneTerrainComp);
}

ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

ecs_system_define(RendTerrainCreateDraw) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (UNLIKELY(!globalItr)) {
    return;
  }
  RendTerrainComp* rendTerrain = ecs_view_write_t(globalItr, RendTerrainComp);
  if (UNLIKELY(!rendTerrain)) {
    rendTerrain = ecs_world_add_t(world, ecs_world_global(world), RendTerrainComp);
  }
  if (UNLIKELY(!rendTerrain->drawEntity)) {
    rendTerrain->drawEntity = rend_terrain_draw_create(world);
    return;
  }

  const SceneTerrainComp* sceneTerrain = ecs_view_read_t(globalItr, SceneTerrainComp);
  if (rendTerrain->terrainVersion == scene_terrain_version(sceneTerrain)) {
    return; // Terrain not changed; no need to re-fill the draw.
  }

  RendDrawComp* draw = ecs_utils_write_t(world, DrawView, rendTerrain->drawEntity, RendDrawComp);
  rend_terrain_draw_init(sceneTerrain, draw);
  rendTerrain->terrainVersion = scene_terrain_version(sceneTerrain);
}

ecs_module_init(rend_terrain_module) {
  ecs_register_comp(RendTerrainComp);

  ecs_register_view(GlobalView);
  ecs_register_view(DrawView);

  ecs_register_system(RendTerrainCreateDraw, ecs_view_id(GlobalView), ecs_view_id(DrawView));
}
