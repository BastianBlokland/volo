#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneCollisionComp);

ecs_module_init(scene_collision_module) { ecs_register_comp(SceneCollisionComp); }

void scene_collision_add_capsule(
    EcsWorld* world, const EcsEntityId entity, const SceneCollisionCapsule capsule) {
  ecs_world_add_t(world, entity, SceneCollisionComp, .data_capsule = capsule);
}

GeoCapsule scene_collision_world_capsule(
    const SceneCollisionCapsule* capsule,
    const SceneTransformComp*    transformComp,
    const SceneScaleComp*        scaleComp) {

  const GeoVector basePos   = LIKELY(transformComp) ? transformComp->position : geo_vector(0);
  const GeoQuat   baseRot   = LIKELY(transformComp) ? transformComp->rotation : geo_quat_ident;
  const f32       baseScale = scaleComp ? scaleComp->scale : 1.0f;

  static const GeoVector g_capsuleDir[] = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}};

  const GeoVector offset = geo_quat_rotate(baseRot, geo_vector_mul(capsule->offset, baseScale));
  const GeoVector dir    = geo_quat_rotate(baseRot, g_capsuleDir[capsule->direction]);

  const GeoVector bottom = geo_vector_add(basePos, offset);
  const GeoVector top    = geo_vector_add(bottom, geo_vector_mul(dir, capsule->height * baseScale));

  return (GeoCapsule){.line = {bottom, top}, .radius = capsule->radius * baseScale};
}
