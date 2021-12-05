#include "scene_transform.h"

ecs_comp_define_public(SceneTransformComp);

GeoMatrix scene_transform_matrix(const SceneTransformComp* comp) {
  const GeoMatrix pos = geo_matrix_translate(comp->position);
  const GeoMatrix rot = geo_matrix_from_quat(comp->rotation);
  return geo_matrix_mul(&pos, &rot);
}

ecs_module_init(scene_transform_module) { ecs_register_comp(SceneTransformComp); }
