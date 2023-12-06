#pragma once
#include "ecs_module.h"

// Forward declare from 'geo_matrix.h'.
typedef union uGeoMatrix GeoMatrix;

ecs_comp_extern(RendLightRendererComp);

f32              rend_light_ambient_intensity(const RendLightRendererComp*);
bool             rend_light_has_shadow(const RendLightRendererComp*);
const GeoMatrix* rend_light_shadow_trans(const RendLightRendererComp*);
const GeoMatrix* rend_light_shadow_proj(const RendLightRendererComp*);
