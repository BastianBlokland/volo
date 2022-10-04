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
} VfxParticle;

/**
 * Global particle renderer.
 */
ecs_comp_extern(VfxParticleRendererComp);

EcsEntityId vfx_particle_atlas(const VfxParticleRendererComp*);
EcsEntityId vfx_particle_draw(const VfxParticleRendererComp*);

void vfx_particle_init(RendDrawComp*, const AssetAtlasComp*);
void vfx_particle_output(RendDrawComp*, const VfxParticle*);
