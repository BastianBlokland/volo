#pragma once
#include "ecs/module.h"
#include "geo/forward.h"

ecs_comp_extern(RendLightRendererComp);

GeoColor         rend_light_ambient_radiance(const RendLightRendererComp*);
bool             rend_light_has_shadow(const RendLightRendererComp*);
const GeoMatrix* rend_light_shadow_trans(const RendLightRendererComp*);
const GeoMatrix* rend_light_shadow_proj(const RendLightRendererComp*);
