#include "scene_transform.h"

ecs_comp_define_public(SceneTransformComp);

ecs_comp_define_public(SceneScaleComp);

ecs_module_init(scene_transform_module) {
  ecs_register_comp(SceneTransformComp);
  ecs_register_comp(SceneScaleComp);
}

GeoMatrix scene_transform_matrix(const SceneTransformComp* trans) {
  const GeoMatrix pos = geo_matrix_translate(trans->position);
  const GeoMatrix rot = geo_matrix_from_quat(trans->rotation);
  return geo_matrix_mul(&pos, &rot);
}

GeoMatrix scene_transform_matrix_inv(const SceneTransformComp* trans) {
  const GeoMatrix rot = geo_matrix_from_quat(geo_quat_inverse(trans->rotation));
  const GeoMatrix pos = geo_matrix_translate(geo_vector_mul(trans->position, -1));
  return geo_matrix_mul(&rot, &pos);
}

GeoMatrix scene_matrix_world(const SceneTransformComp* trans, const SceneScaleComp* scale) {
  const GeoVector pos      = LIKELY(trans) ? trans->position : geo_vector(0);
  const GeoQuat   rot      = LIKELY(trans) ? trans->rotation : geo_quat_ident;
  const f32       scaleMag = scale ? scale->scale : 1.0f;
  return geo_matrix_trs(pos, rot, geo_vector(scaleMag, scaleMag, scaleMag));
}
