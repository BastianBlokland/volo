#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

// Forward declare from 'rend_draw.h'.
ecs_comp_extern(RendDrawComp);

typedef struct {
  GeoVector position;
  GeoQuat   rotation;
  f32       sizeX, sizeY;
  GeoColor  color;
} VfxParticle;

/**
 * Global particle renderer.
 */
ecs_comp_extern(VfxParticleRendererComp);

/**
 * Retrieve the draw entity.
 */
EcsEntityId vfx_particle_draw(const VfxParticleRendererComp*);

/**
 * Emit a particle to the draw.
 */
void vfx_particle_output(RendDrawComp*, const VfxParticle*);
