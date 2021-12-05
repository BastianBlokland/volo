#include "scene_transform.h"

ecs_comp_define_public(SceneTransformComp);

GeoMatrix scene_transform_matrix(const SceneTransformComp* trans) {
  const GeoMatrix pos = geo_matrix_translate(trans->position);
  const GeoMatrix rot = geo_matrix_from_quat(trans->rotation);
  return geo_matrix_mul(&pos, &rot);
}

GeoMatrix scene_transform_matrix_inv(const SceneTransformComp* trans) {
  const GeoMatrix rot = geo_matrix_from_quat(geo_quat_inv(trans->rotation));
  const GeoMatrix pos = geo_matrix_translate(geo_vector_mul(trans->position, -1));
  return geo_matrix_mul(&rot, &pos);
}

ecs_module_init(scene_transform_module) { ecs_register_comp(SceneTransformComp); }
