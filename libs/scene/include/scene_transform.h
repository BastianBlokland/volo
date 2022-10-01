#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"
#include "geo_quat.h"
#include "geo_vector.h"

ecs_comp_extern_public(SceneTransformComp) {
  GeoVector position;
  GeoQuat   rotation;
};

ecs_comp_extern_public(SceneScaleComp) { f32 scale; };

GeoMatrix scene_transform_matrix(const SceneTransformComp*);
GeoMatrix scene_transform_matrix_inv(const SceneTransformComp*);

/**
 * Compute the world-space matrix for the given transform and scale.
 * NOTE: Both transform and scale are optional.
 */
GeoMatrix scene_matrix_world(const SceneTransformComp*, const SceneScaleComp*);
