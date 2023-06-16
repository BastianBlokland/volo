#include "asset_manager.h"
#include "core_math.h"
#include "ecs_world.h"
#include "geo_matrix.h"
#include "rend_draw.h"
#include "rend_register.h"
#include "scene_faction.h"
#include "scene_transform.h"
#include "scene_visibility.h"

#include "fog_internal.h"

static const String g_fogVisionGraphic = string_static("graphics/fog_vision.gra");
static const GeoBox g_fogBounds        = {
    .min = {.x = -225.0f, .y = -100.0f, .z = -225.0f},
    .max = {.x = 225.0f, .y = 100.0f, .z = 225.0f},
};

ecs_comp_define(RendFogComp) {
  EcsEntityId drawEntity;
  GeoMatrix   transMatrix, projMatrix;
};

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_read(RendFogComp);
}
ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

ecs_view_define(VisionEntityView) {
  ecs_access_read(SceneFactionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneVisionComp);
}

static EcsEntityId rend_fog_draw_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId entity        = ecs_world_entity_create(world);
  RendDrawComp*     draw          = rend_draw_create(world, entity, RendDrawFlags_FogVision);
  const EcsEntityId graphicEntity = asset_lookup(world, assets, g_fogVisionGraphic);
  rend_draw_set_graphic(draw, graphicEntity);
  return entity;
}

static void rend_fog_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId global = ecs_world_global(world);
  ecs_world_add_t(
      world,
      global,
      RendFogComp,
      .drawEntity  = rend_fog_draw_create(world, assets),
      .transMatrix = geo_matrix_rotate_x(math_pi_f32 * 0.5f),
      .projMatrix  = geo_matrix_proj_ortho_box(
          g_fogBounds.min.x,
          g_fogBounds.max.x,
          g_fogBounds.min.z,
          g_fogBounds.max.z,
          g_fogBounds.min.y,
          g_fogBounds.max.y));
}

ecs_system_define(RendFogRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not yet available.
  }

  AssetManagerComp*  assets = ecs_view_write_t(globalItr, AssetManagerComp);
  const RendFogComp* fog    = ecs_view_read_t(globalItr, RendFogComp);
  if (!fog) {
    rend_fog_create(world, assets);
    return;
  }

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

    const GeoBox bounds = geo_box_from_sphere(trans->position, vision->radius);
    *rend_draw_add_instance_t(draw, FogVisionData, SceneTags_None, bounds) = (FogVisionData){
        .data1.x = trans->position.x,
        .data1.y = trans->position.y,
        .data1.z = trans->position.z,
        .data1.w = vision->radius,
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
