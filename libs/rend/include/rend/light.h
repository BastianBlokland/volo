#pragma once
#include "ecs/module.h"
#include "geo/forward.h"
#include "geo/vector.h"

typedef enum {
  RendLightFlags_None         = 0,
  RendLightFlags_Shadow       = 1 << 0,
  RendLightFlags_CoverageMask = 1 << 1,
} RendLightFlags;

typedef enum {
  RendLightDebug_ShadowFrustumTarget,
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

// clang-format off

/**
 * Light primitives.
 */
void rend_light_directional(RendLightComp*, GeoQuat rot, GeoColor radiance, RendLightFlags);
void rend_light_point(RendLightComp*, GeoVector pos, GeoColor radiance, f32 radius, RendLightFlags);
void rend_light_spot(RendLightComp*, GeoVector posA, GeoVector posB, GeoColor radiance, f32 angle, RendLightFlags);
void rend_light_line(RendLightComp*, GeoVector posA, GeoVector posB, GeoColor radiance, f32 radius, RendLightFlags);
void rend_light_ambient(RendLightComp*, GeoColor radiance);

// clang-format on
