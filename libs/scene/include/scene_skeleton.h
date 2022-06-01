#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"

ecs_comp_extern_public(SceneSkeletonTemplateComp) {
  u32              jointCount;
  const GeoMatrix* jointInvBindTransforms;
};

ecs_comp_extern_public(SceneSkeletonComp) {
  u32        jointCount;
  GeoMatrix* jointTransforms;
};
