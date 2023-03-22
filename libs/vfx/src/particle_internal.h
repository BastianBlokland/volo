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

/**
 * NOTE: Flag values are used in GLSL, update the GLSL side when changing these.
 */
typedef enum {
  VfxParticle_GeometryFade      = 1 << 0, // Aka 'soft particles'.
  VfxParticle_BillboardSphere   = 1 << 1,
  VfxParticle_BillboardCylinder = 1 << 2,
  VfxParticle_Billboard         = VfxParticle_BillboardSphere | VfxParticle_BillboardCylinder,
  VfxParticle_ShadowCaster      = 1 << 3,
} VfxParticleFlags;

typedef struct {
  GeoVector        position;
  GeoQuat          rotation;
  VfxParticleFlags flags : 16;
  u16              atlasIndex;
  f32              sizeX, sizeY;
  f32              opacity;
  GeoColor         color;
} VfxParticle;

/**
 * Initialize a particle draw.
 * NOTE: NOT thread-safe, should be called only once per frame.
 */
void vfx_particle_init(RendDrawComp*, const AssetAtlasComp*);

/**
 * Output a particle to the given draw.
 */
void vfx_particle_output(RendDrawComp*, const VfxParticle*);
