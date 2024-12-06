#pragma once
#include "ecs_module.h"
#include "geo.h"

ecs_comp_extern(RendFogComp);

bool             rend_fog_active(const RendFogComp*);
const GeoMatrix* rend_fog_trans(const RendFogComp*);
const GeoMatrix* rend_fog_proj(const RendFogComp*);
