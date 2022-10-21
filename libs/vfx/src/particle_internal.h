#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

// Forward declare from 'rend_draw.h'.
ecs_comp_extern(RendDrawComp);

// Forward declare from 'asset_atlas.h'.
ecs_comp_extern(AssetAtlasComp);

typedef struct {
  GeoVector position;
  GeoQuat   rotation;
  u32       atlasIndex;
  f32       sizeX, sizeY;
  GeoColor  color;
  f32       opacity;
} VfxParticle;

/**
 * Global particle renderer.
 */
ecs_comp_extern(VfxParticleRendererComp);

EcsEntityId vfx_particle_atlas(const VfxParticleRendererComp*);
EcsEntityId vfx_particle_draw(const VfxParticleRendererComp*);

/**
 * Initialize a particle draw.
 * NOTE: NOT thread-safe, should be called only once per frame.
 */
void vfx_particle_init(RendDrawComp*, const AssetAtlasComp*);

/**
 * Output a particle to the given draw.
 * NOTE: Thread-safe, multiple particles can be added to the same draw in parallel.
 */
void vfx_particle_output(RendDrawComp*, const VfxParticle*);
