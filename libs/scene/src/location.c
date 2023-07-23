#include "ecs_world.h"
#include "scene_location.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneLocationComp);

ecs_module_init(scene_location_module) { ecs_register_comp(SceneLocationComp); }

GeoVector scene_location(
    const SceneLocationComp*  locComp,
    const SceneTransformComp* transComp,
    const SceneScaleComp*     scaleComp,
    const SceneLocationType   type) {
  const f32       scale       = scaleComp ? scaleComp->scale : 1.0f;
  const GeoVector offsetLocal = locComp->offsets[type];
  return geo_vector_add(
      transComp->position,
      geo_quat_rotate(transComp->rotation, geo_vector_mul(offsetLocal, scale)));
}

GeoVector scene_location_predict(
    const SceneLocationComp*  locComp,
    const SceneTransformComp* transComp,
    const SceneScaleComp*     scaleComp,
    const SceneVelocityComp*  veloComp,
    const SceneLocationType   type,
    const TimeDuration        time) {
  const f32       scale       = scaleComp ? scaleComp->scale : 1.0f;
  const GeoVector offsetLocal = locComp->offsets[type];
  return geo_vector_add(
      scene_position_predict(transComp, veloComp, time),
      geo_quat_rotate(transComp->rotation, geo_vector_mul(offsetLocal, scale)));
}
