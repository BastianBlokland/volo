#include "ecs_utils.h"
#include "ecs_world.h"
#include "rend_draw.h"
#include "scene_terrain.h"

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
} RendTerrainPatchData;

ASSERT(sizeof(RendTerrainPatchData) == 16, "Size needs to match the size defined in glsl");

ecs_comp_define(RendTerrainComp) {
  u32         terrainVersion;
  EcsEntityId drawEntity;
};

static EcsEntityId rend_terrain_draw_create(EcsWorld* world) {
  EcsEntityId         e     = ecs_world_entity_create(world);
  const RendDrawFlags flags = RendDrawFlags_NoAutoClear;
  rend_draw_create(world, e, flags);
  return e;
}

static void rend_terrain_draw_init(const SceneTerrainComp* terrain, RendDrawComp* draw) {
  // Set global terrain meta.
  rend_draw_set_graphic(draw, scene_terrain_graphic(terrain));
  *rend_draw_set_data_t(draw, RendTerrainData) = (RendTerrainData){
      .size        = scene_terrain_size(terrain),
      .heightScale = scene_terrain_height_scale(terrain),
      .patchScale  = scene_terrain_size(terrain),
  };

  // Clear previously added instances.
  rend_draw_clear(draw);

  // Add patch instances.
  const SceneTags tags                                                = SceneTags_Terrain;
  const GeoBox    bounds                                              = geo_box_inverted3();
  *rend_draw_add_instance_t(draw, RendTerrainPatchData, tags, bounds) = (RendTerrainPatchData){
      .posX = 0,
      .posZ = 0,
  };
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
