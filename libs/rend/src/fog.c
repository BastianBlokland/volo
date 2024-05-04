#include "asset_manager.h"
#include "core_math.h"
#include "ecs_world.h"
#include "geo_matrix.h"
#include "rend_draw.h"
#include "rend_register.h"
#include "rend_settings.h"
#include "scene_faction.h"
#include "scene_terrain.h"
#include "scene_transform.h"
#include "scene_visibility.h"

#include "fog_internal.h"

static const String g_fogVisionGraphic = string_static("graphics/fog_vision.graphic");
static const f32    g_worldHeight      = 100.0f;

ecs_comp_define(RendFogComp) {
  EcsEntityId drawEntity;
  GeoMatrix   transMatrix, projMatrix;
};

ecs_view_define(GlobalView) {
  ecs_access_maybe_write(RendFogComp);
  ecs_access_read(RendSettingsGlobalComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(DrawView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the draw's we create.
  ecs_access_write(RendDrawComp);
}

ecs_view_define(VisionEntityView) {
  ecs_access_read(SceneFactionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneVisionComp);
}

static EcsEntityId rend_fog_draw_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId entity        = ecs_world_entity_create(world);
  RendDrawComp*     draw          = rend_draw_create(world, entity, RendDrawFlags_FogVision);
  const EcsEntityId graphicEntity = asset_lookup(world, assets, g_fogVisionGraphic);
  rend_draw_set_resource(draw, RendDrawResource_Graphic, graphicEntity);
  return entity;
}

static void rend_fog_update_proj(RendFogComp* fog, const SceneTerrainComp* terrain) {
  GeoBox bounds;
  if (scene_terrain_loaded(terrain)) {
    const GeoBox terrainBounds = scene_terrain_bounds(terrain);
    bounds                     = geo_box_dilate(&terrainBounds, geo_vector(0, g_worldHeight, 0));
  } else {
    bounds = geo_box_from_center(geo_vector(0), geo_vector(500, 100, 500));
  }
  fog->projMatrix = geo_matrix_proj_ortho_box(
      bounds.min.x, bounds.max.x, bounds.max.z, bounds.min.z, bounds.min.y, bounds.max.y);
}

static void rend_fog_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId global = ecs_world_global(world);

  const EcsEntityId drawEntity = rend_fog_draw_create(world, assets);
  ecs_world_add_t(
      world,
      global,
      RendFogComp,
      .drawEntity  = drawEntity,
      .transMatrix = geo_matrix_rotate_x(math_pi_f32 * 0.5f));
}

ecs_system_define(RendFogRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not yet available.
  }

  const RendSettingsGlobalComp* settingsGlobal = ecs_view_read_t(globalItr, RendSettingsGlobalComp);
  AssetManagerComp*             assets         = ecs_view_write_t(globalItr, AssetManagerComp);
  RendFogComp*                  fog            = ecs_view_write_t(globalItr, RendFogComp);
  const SceneTerrainComp*       terrain        = ecs_view_read_t(globalItr, SceneTerrainComp);
  if (!fog) {
    rend_fog_create(world, assets);
    return;
  }

  rend_fog_update_proj(fog, terrain);

  EcsView*      drawView = ecs_world_view_t(world, DrawView);
  EcsIterator*  drawItr  = ecs_view_at(drawView, fog->drawEntity);
  RendDrawComp* draw     = ecs_view_write_t(drawItr, RendDrawComp);

  EcsView* visionEntityView = ecs_world_view_t(world, VisionEntityView);
  for (EcsIterator* itr = ecs_view_itr(visionEntityView); ecs_view_walk(itr);) {
    const SceneVisionComp*    vision  = ecs_view_read_t(itr, SceneVisionComp);
    const SceneTransformComp* trans   = ecs_view_read_t(itr, SceneTransformComp);
    const SceneFactionComp*   faction = ecs_view_read_t(itr, SceneFactionComp);

    if (faction->id != SceneFaction_A) {
      continue; // TODO: Make the local faction configurable instead of hardcoding 'A'.
    }

    typedef struct {
      ALIGNAS(16)
      GeoVector data1; // x, y, z: position, w: radius.
    } FogVisionData;
    ASSERT(sizeof(FogVisionData) == 16, "Size needs to match the size defined in glsl");

    const GeoBox visBounds = geo_box_from_sphere(trans->position, vision->radius);
    *rend_draw_add_instance_t(draw, FogVisionData, SceneTags_None, visBounds) = (FogVisionData){
        .data1.x = trans->position.x,
        .data1.y = trans->position.y,
        .data1.z = trans->position.z,
        .data1.w = vision->radius + settingsGlobal->fogDilation,
    };
  }
}

ecs_module_init(rend_fog_module) {
  ecs_register_comp(RendFogComp);

  ecs_register_view(GlobalView);
  ecs_register_view(DrawView);
  ecs_register_view(VisionEntityView);

  ecs_register_system(
      RendFogRenderSys,
      ecs_view_id(GlobalView),
      ecs_view_id(DrawView),
      ecs_view_id(VisionEntityView));

  ecs_order(RendFogRenderSys, RendOrder_DrawCollect);
}

const GeoMatrix* rend_fog_trans(const RendFogComp* fog) { return &fog->transMatrix; }

const GeoMatrix* rend_fog_proj(const RendFogComp* fog) { return &fog->projMatrix; }
