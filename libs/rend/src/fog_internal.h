#pragma once
#include "ecs_module.h"

// Forward declare from 'geo_matrix.h'.
typedef union uGeoMatrix GeoMatrix;

ecs_comp_extern(RendFogComp);

bool             rend_fog_active(const RendFogComp*);
const GeoMatrix* rend_fog_trans(const RendFogComp*);
const GeoMatrix* rend_fog_proj(const RendFogComp*);
