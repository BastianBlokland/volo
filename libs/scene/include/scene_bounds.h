#pragma once
#include "ecs_module.h"
#include "geo_box.h"

ecs_comp_extern_public(SceneBoundsComp) { GeoBox local; };
