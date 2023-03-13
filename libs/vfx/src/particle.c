#include "asset_atlas.h"
#include "asset_manager.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_world.h"
#include "rend_draw.h"
#include "scene_tag.h"

#include "particle_internal.h"

// clang-format off
static const String g_vfxParticleGraphics[VfxParticleType_Count] = {
    [VfxParticleType_Forward]    = string_static("graphics/vfx/particle_forward.gra"),
    [VfxParticleType_Distortion] = string_static("graphics/vfx/particle_distortion.gra"),
};
static const RendDrawFlags g_vfxParticleDrawFlags[VfxParticleType_Count] = {
    [VfxParticleType_Forward]    = RendDrawFlags_Particle | RendDrawFlags_Preload | RendDrawFlags_SortBackToFront,
    [VfxParticleType_Distortion] = RendDrawFlags_Particle | RendDrawFlags_Preload | RendDrawFlags_Distortion,
};
// clang-format on

typedef struct {
  ALIGNAS(16)
  f32 atlasEntriesPerDim;
  f32 atlasEntrySize;
  f32 atlasEntrySizeMinusPadding;
  f32 atlasEntryPadding;
} VfxParticleMetaData;

ASSERT(sizeof(VfxParticleMetaData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  GeoVector data1;    // xyz: position, w: atlasIndex.
  f16       data2[4]; // xyzw: rotation quaternion.
  f16       data3[4]; // xy: scale, z: opacity, w: flags.
  f16       data4[4]; // xyzw: color.
} VfxParticleData;

ASSERT(sizeof(VfxParticleData) == 48, "Size needs to match the size defined in glsl");
ASSERT(alignof(VfxParticleData) == 16, "Alignment needs to match the glsl alignment");

ecs_comp_define(VfxParticleRendererComp) { EcsEntityId drawEntities[VfxParticleType_Count]; };

ecs_comp_define(VfxParticleDrawComp);

static EcsEntityId
vfx_particle_draw_create(EcsWorld* world, AssetManagerComp* assets, const VfxParticleType type) {
  const EcsEntityId entity = asset_lookup(world, assets, g_vfxParticleGraphics[type]);
  ecs_world_add_empty_t(world, entity, VfxParticleDrawComp);
  RendDrawComp* draw = rend_draw_create(world, entity, g_vfxParticleDrawFlags[type]);
  rend_draw_set_graphic(draw, entity); // Graphic is on the same entity as the draw.
  return entity;
}

ecs_view_define(GlobalView) {
  ecs_access_maybe_write(VfxParticleRendererComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(VfxParticleRendererInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*        assets   = ecs_view_write_t(globalItr, AssetManagerComp);
  VfxParticleRendererComp* renderer = ecs_view_write_t(globalItr, VfxParticleRendererComp);

  if (!renderer) {
    renderer = ecs_world_add_t(world, ecs_world_global(world), VfxParticleRendererComp);
    for (VfxParticleType type = 0; type != VfxParticleType_Count; ++type) {
      renderer->drawEntities[type] = vfx_particle_draw_create(world, assets, type);
    }
  }
}

ecs_module_init(vfx_particle_module) {
  ecs_register_comp(VfxParticleRendererComp);
  ecs_register_comp_empty(VfxParticleDrawComp);

  ecs_register_view(GlobalView);

  ecs_register_system(VfxParticleRendererInitSys, ecs_view_id(GlobalView));
}

EcsEntityId vfx_particle_draw(const VfxParticleRendererComp* renderer, const VfxParticleType type) {
  diag_assert(type < VfxParticleType_Count);
  return renderer->drawEntities[type];
}

void vfx_particle_init(RendDrawComp* draw, const AssetAtlasComp* atlas) {
  const f32 atlasEntrySize             = 1.0f / atlas->entriesPerDim;
  const f32 atlasEntrySizeMinusPadding = atlasEntrySize - atlas->entryPadding * 2;

  *rend_draw_set_data_t(draw, VfxParticleMetaData) = (VfxParticleMetaData){
      .atlasEntriesPerDim         = atlas->entriesPerDim,
      .atlasEntrySize             = atlasEntrySize,
      .atlasEntrySizeMinusPadding = atlasEntrySizeMinusPadding,
      .atlasEntryPadding          = atlas->entryPadding,
  };
}

void vfx_particle_output(RendDrawComp* draw, const VfxParticle* p) {
  GeoBox bounds;
  if (p->flags & VfxParticle_Billboard) {
    bounds = geo_box_from_sphere(p->position, math_max(p->sizeX, p->sizeY));
  } else {
    bounds = geo_box_from_quad(p->position, p->sizeX, p->sizeY, p->rotation);
  }

  SceneTags tags = SceneTags_Vfx;
  if (p->flags & VfxParticle_ShadowCaster) {
    tags |= SceneTags_ShadowCaster;
  }
  VfxParticleData* data = rend_draw_add_instance_t(draw, VfxParticleData, tags, bounds);
  data->data1           = p->position;
  data->data1.w         = (f32)p->atlasIndex;

  geo_quat_pack_f16(p->rotation, data->data2);
  data->data3[0] = float_f32_to_f16(p->sizeX);
  data->data3[1] = float_f32_to_f16(p->sizeY);
  data->data3[2] = float_f32_to_f16(p->opacity);

  diag_assert_msg(p->flags <= 1024, "Flags are not exactly representable by a 16 bit float");
  data->data3[3] = float_f32_to_f16((f32)p->flags);

  geo_color_pack_f16(p->color, data->data4);
}
