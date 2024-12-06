#pragma once
#include "ecs_module.h"
#include "geo.h"
#include "geo_quat.h"
#include "geo_vector.h"

ecs_comp_extern_public(SceneTransformComp) {
  GeoVector position;
  GeoQuat   rotation;
};

ecs_comp_extern_public(SceneScaleComp) { f32 scale; };

/**
 * Velocity of the entity in the last frame.
 * Updated automatically based on position changes.
 */
ecs_comp_extern_public(SceneVelocityComp) {
  GeoVector lastPosition;
  GeoVector velocityAvg;
};

GeoVector scene_transform_to_world(const SceneTransformComp*, const SceneScaleComp*, GeoVector pos);

void scene_transform_rotate_around(SceneTransformComp*, GeoVector pivot, GeoQuat);
void scene_transform_scale_around(
    SceneTransformComp*, SceneScaleComp*, GeoVector pivot, f32 scaleDelta);

GeoMatrix scene_transform_matrix(const SceneTransformComp*);
GeoMatrix scene_transform_matrix_inv(const SceneTransformComp*);

/**
 * Compute the world-space matrix for the given transform and scale.
 * NOTE: Both transform and scale are optional.
 */
GeoMatrix scene_matrix_world(const SceneTransformComp*, const SceneScaleComp*);

/**
 * Predict a position in the future based on the current position and velocity.
 * NOTE: 'SceneVelocityComp' is optional, if not provided the current position will be returned.
 */
GeoVector scene_position_predict(
    const SceneTransformComp*, const SceneVelocityComp*, TimeDuration timeInFuture);
