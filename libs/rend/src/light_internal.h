#pragma once
#include "ecs_module.h"
#include "geo.h"

ecs_comp_extern(RendLightRendererComp);

f32              rend_light_ambient_intensity(const RendLightRendererComp*);
bool             rend_light_has_shadow(const RendLightRendererComp*);
const GeoMatrix* rend_light_shadow_trans(const RendLightRendererComp*);
const GeoMatrix* rend_light_shadow_proj(const RendLightRendererComp*);
