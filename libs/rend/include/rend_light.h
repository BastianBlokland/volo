#pragma once
#include "ecs_module.h"
#include "geo.h"

typedef enum {
  RendLightFlags_None         = 0,
  RendLightFlags_Shadow       = 1 << 0,
  RendLightFlags_CoverageMask = 1 << 1,
} RendLightFlags;

ecs_comp_extern(RendLightComp);

/**
 * Add a new light component to the given entity.
 */
RendLightComp* rend_light_create(EcsWorld*, EcsEntityId entity);

/**
 * Light primitives.
 */
void rend_light_directional(RendLightComp*, GeoQuat rot, GeoColor radiance, RendLightFlags);
void rend_light_point(RendLightComp*, GeoVector pos, GeoColor radiance, f32 radius, RendLightFlags);
void rend_light_ambient(RendLightComp*, f32 intensity);
