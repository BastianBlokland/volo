#pragma once
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

void vfx_particle_output(RendDrawComp*, const VfxParticle*);
