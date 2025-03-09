#pragma once
#include "ecs_module.h"
#include "geo.h"
#include "geo_vector.h"

typedef enum {
  RendLightFlags_None         = 0,
  RendLightFlags_Shadow       = 1 << 0,
  RendLightFlags_CoverageMask = 1 << 1,
} RendLightFlags;

typedef enum {
  RendLightDebug_ShadowFrustum,
} RendLightDebugType;

typedef struct {
  RendLightDebugType type;
  GeoVector          frustum[8];
} RendLightDebug;

ecs_comp_extern(RendLightComp);

/**
 * Add a new light component to the given entity.
 */
RendLightComp* rend_light_create(EcsWorld*, EcsEntityId entity);

/**
 * Query debug data for the given light component.
 */
usize                 rend_light_debug_count(const RendLightComp*);
const RendLightDebug* rend_light_debug_data(const RendLightComp*);

/**
 * Light primitives.
 */
void rend_light_directional(RendLightComp*, GeoQuat rot, GeoColor radiance, RendLightFlags);
void rend_light_point(RendLightComp*, GeoVector pos, GeoColor radiance, f32 radius, RendLightFlags);
void rend_light_ambient(RendLightComp*, f32 intensity);
