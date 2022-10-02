#include "asset_manager.h"
#include "ecs_world.h"
#include "rend_draw.h"
#include "scene_tag.h"

#include "particle_internal.h"

static const String g_vfxParticleGraphic = string_static("graphics/vfx/particle.gra");

typedef struct {
  ALIGNAS(16)
  GeoVector data1;    // xyz: position.
  f16       data2[4]; // xyzw: rotation quaternion.
  f16       data3[4]; // xy: scale.
  f16       data4[4]; // xyzw: color.
} VfxParticleData;

ASSERT(sizeof(VfxParticleData) == 48, "Size needs to match the size defined in glsl");

ecs_comp_define(VfxParticleRendererComp) { EcsEntityId drawEntity; };

static EcsEntityId vfx_particle_draw_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId   entity = asset_lookup(world, assets, g_vfxParticleGraphic);
  const RendDrawFlags flags  = RendDrawFlags_SortBackToFront;
  RendDrawComp*       draw   = rend_draw_create(world, entity, flags);
  rend_draw_set_graphic(draw, entity); // Graphic is on the same entity as the draw.
  return entity;
}

ecs_view_define(InitGlobalView) {
  ecs_access_without(VfxParticleRendererComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(VfxParticleRendererInitSys) {
  EcsView*     initGlobalView = ecs_world_view_t(world, InitGlobalView);
  EcsIterator* initGlobalItr  = ecs_view_maybe_at(initGlobalView, ecs_world_global(world));
  if (!initGlobalItr) {
    return;
  }
  AssetManagerComp* assets     = ecs_view_write_t(initGlobalItr, AssetManagerComp);
  const EcsEntityId drawEntity = vfx_particle_draw_create(world, assets);

  ecs_world_add_t(
      world, ecs_world_global(world), VfxParticleRendererComp, .drawEntity = drawEntity);
}

ecs_module_init(vfx_particle_module) {
  ecs_register_comp(VfxParticleRendererComp);

  ecs_register_view(InitGlobalView);

  ecs_register_system(VfxParticleRendererInitSys, ecs_view_id(InitGlobalView));
}

EcsEntityId vfx_particle_draw(const VfxParticleRendererComp* renderer) {
  return renderer->drawEntity;
}

void vfx_particle_output(RendDrawComp* draw, const VfxParticle* particle) {
  VfxParticleData instData;
  instData.data1 = particle->position;
  geo_quat_pack_f16(particle->rotation, instData.data2);
  instData.data3[0] = float_f32_to_f16(particle->sizeX);
  instData.data3[1] = float_f32_to_f16(particle->sizeY);
  geo_color_pack_f16(particle->color, instData.data4);

  const GeoBox bounds =
      geo_box_from_quad(particle->position, particle->sizeX, particle->sizeY, particle->rotation);

  rend_draw_add_instance(draw, mem_var(instData), SceneTags_Vfx, bounds);
}
