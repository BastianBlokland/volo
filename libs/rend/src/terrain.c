#include "asset_manager.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "rend_object.h"
#include "scene_tag.h"
#include "scene_terrain.h"

// clang-format off

static const f32    g_terrainPatchTargetSize = 25.0f;
static const String g_terrainDebugWireframe  = string_static("graphics/debug/wireframe_terrain.graphic");

// clang-format on

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
  EcsEntityId objEntity;
};

static EcsEntityId rend_terrain_obj_create(EcsWorld* world, AssetManagerComp* assets) {
  EcsEntityId     e   = ecs_world_entity_create(world);
  RendObjectComp* obj = rend_object_create(world, e, RendObjectFlags_NoAutoClear);
  rend_object_set_resource(
      obj,
      RendObjectRes_GraphicDebugWireframe,
      asset_lookup(world, assets, g_terrainDebugWireframe));
  return e;
}

static void rend_terrain_obj_update(const SceneTerrainComp* sceneTerrain, RendObjectComp* obj) {
  const EcsEntityId graphic = scene_terrain_resource_graphic(sceneTerrain);
  if (!graphic) {
    rend_object_clear(obj);
    return;
  }
  const EcsEntityId heightmap = scene_terrain_resource_heightmap(sceneTerrain);
  diag_assert(heightmap);

  const f32 size           = scene_terrain_size(sceneTerrain);
  const f32 halfSize       = size * 0.5f;
  const u32 patchCountAxis = (u32)math_round_up_f32(size / g_terrainPatchTargetSize);
  const f32 patchScale     = 1.0f / patchCountAxis;
  const f32 patchSize      = size * patchScale;
  const f32 patchHalfSize  = patchSize * 0.5f;
  const f32 heightScale    = scene_terrain_height_max(sceneTerrain);

  // Set global terrain meta.
  rend_object_set_resource(obj, RendObjectRes_Graphic, graphic);
  rend_object_set_resource(obj, RendObjectRes_Texture, heightmap);
  *rend_object_set_data_t(obj, RendTerrainData) = (RendTerrainData){
      .size        = size,
      .heightScale = heightScale,
      .patchScale  = patchScale,
  };

  // Clear previously added instances.
  rend_object_clear(obj);

  // Add patch instances.
  const SceneTags patchTags = SceneTags_Terrain;
  for (u32 x = 0; x != patchCountAxis; ++x) {
    for (u32 z = 0; z != patchCountAxis; ++z) {
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
      *rend_object_add_instance_t(obj, RendTerrainPatchData, patchTags, patchBounds) = patchData;
    }
  }
}

ecs_view_define(GlobalView) {
  ecs_access_maybe_write(RendTerrainComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(ObjView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the objects we create.
  ecs_access_write(RendObjectComp);
}

ecs_system_define(RendTerrainCreateDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (UNLIKELY(!globalItr)) {
    return;
  }
  AssetManagerComp* assetManager = ecs_view_write_t(globalItr, AssetManagerComp);
  RendTerrainComp*  rendTerrain  = ecs_view_write_t(globalItr, RendTerrainComp);
  if (UNLIKELY(!rendTerrain)) {
    rendTerrain = ecs_world_add_t(world, ecs_world_global(world), RendTerrainComp);
  }
  if (UNLIKELY(!rendTerrain->objEntity)) {
    rendTerrain->objEntity = rend_terrain_obj_create(world, assetManager);
    return;
  }

  RendObjectComp* obj = ecs_utils_write_t(world, ObjView, rendTerrain->objEntity, RendObjectComp);

  const SceneTerrainComp* sceneTerrain = ecs_view_read_t(globalItr, SceneTerrainComp);
  if (rendTerrain->terrainVersion != scene_terrain_version(sceneTerrain)) {
    rend_terrain_obj_update(sceneTerrain, obj);
    rendTerrain->terrainVersion = scene_terrain_version(sceneTerrain);
  }
}

ecs_module_init(rend_terrain_module) {
  ecs_register_comp(RendTerrainComp);

  ecs_register_view(GlobalView);
  ecs_register_view(ObjView);

  ecs_register_system(RendTerrainCreateDrawSys, ecs_view_id(GlobalView), ecs_view_id(ObjView));
}
