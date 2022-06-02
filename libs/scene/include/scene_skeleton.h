#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"

ecs_comp_extern(SceneSkeletonTemplateComp);

ecs_comp_extern_public(SceneSkeletonComp) {
  u32        jointCount;
  GeoMatrix* jointTransforms;
};
