#include "asset_atlas.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_world.h"
#include "rend_draw.h"
#include "scene_tag.h"

#include "atlas_internal.h"
#include "particle_internal.h"

typedef struct {
  VfxAtlasDrawData atlas;
} VfxParticleMetaData;

ASSERT(sizeof(VfxParticleMetaData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  f32 data1[4]; // xyz: position, w: atlasIndex.
  f16 data2[4]; // xyzw: rotation quaternion.
  f16 data3[4]; // xy: scale, z: opacity, w: flags.
  f16 data4[4]; // xyzw: color.
} VfxParticleData;

ASSERT(sizeof(VfxParticleData) == 48, "Size needs to match the size defined in glsl");

void vfx_particle_init(RendDrawComp* draw, const AssetAtlasComp* atlas) {
  *rend_draw_set_data_t(draw, VfxParticleMetaData) = (VfxParticleMetaData){
      .atlas = vfx_atlas_draw_data(atlas),
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
  data->data1[0]        = p->position.x;
  data->data1[1]        = p->position.y;
  data->data1[2]        = p->position.z;
  data->data1[3]        = (f32)p->atlasIndex;

  geo_quat_pack_f16(p->rotation, data->data2);

  diag_assert_msg(p->flags <= 1024, "Flags are not exactly representable by a 16 bit float");
  geo_vector_pack_f16(geo_vector(p->sizeX, p->sizeY, p->opacity, (f32)p->flags), data->data3);

  geo_color_pack_f16(p->color, data->data4);
}
