#include "rend_draw.h"
#include "scene_tag.h"

#include "particle_internal.h"

typedef struct {
  ALIGNAS(16)
  GeoVector position;
  GeoQuat   rotation;
  GeoVector scale;
  GeoColor  color;
} VfxParticleData;

ASSERT(sizeof(VfxParticleData) == 64, "Size needs to match the size defined in glsl");

void vfx_particle_output(RendDrawComp* draw, const VfxParticle* particle) {
  const VfxParticleData data = {
      .position = particle->position,
      .rotation = particle->rotation,
      .scale    = geo_vector(particle->sizeX, particle->sizeY),
      .color    = particle->color,
  };
  const GeoBox bounds =
      geo_box_from_quad(particle->position, particle->sizeX, particle->sizeY, particle->rotation);

  rend_draw_add_instance(draw, mem_var(data), SceneTags_Vfx, bounds);
}
