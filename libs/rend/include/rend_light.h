#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"

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

// clang-format off

/**
 * Light primitives.
 *
 * Useful starting points for the attenuation values:
 * - https://wiki.ogre3d.org/tiki-index.php?page=-Point+Light+Attenuation
 */
void rend_light_directional(RendLightComp*, GeoQuat rot, GeoColor radiance, RendLightFlags);
void rend_light_point(RendLightComp*, GeoVector pos, GeoColor radiance, f32 attenuationLinear, f32 attenuationQuadratic, RendLightFlags);

// clang-format on
