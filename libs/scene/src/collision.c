#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "geo_query.h"
#include "scene_collision.h"
#include "scene_register.h"
#include "scene_transform.h"

ASSERT(sizeof(EcsEntityId) <= sizeof(u64), "EntityId's have to be storable with 64 bit integers");

ecs_comp_define(SceneCollisionEnvComp) { GeoQueryEnv* queryEnv; };

ecs_comp_define_public(SceneCollisionComp);

static void ecs_destruct_collision_env_comp(void* data) {
  SceneCollisionEnvComp* env = data;
  geo_query_env_destroy(env->queryEnv);
}

ecs_view_define(UpdateView) { ecs_access_write(SceneCollisionEnvComp); }

ecs_view_define(CollisionEntityView) {
  ecs_access_read(SceneCollisionComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
}

static SceneCollisionEnvComp* collision_env_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, UpdateView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  if (LIKELY(itr)) {
    return ecs_view_write_t(itr, SceneCollisionEnvComp);
  }
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneCollisionEnvComp,
      .queryEnv = geo_query_env_create(g_alloc_heap));
}

ecs_system_define(SceneCollisionUpdateSys) {
  SceneCollisionEnvComp* env = collision_env_get_or_create(world);

  geo_query_env_clear(env->queryEnv);

  EcsView* collisionEntities = ecs_world_view_t(world, CollisionEntityView);
  for (EcsIterator* itr = ecs_view_itr(collisionEntities); ecs_view_walk(itr);) {
    const SceneCollisionComp* collision = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale     = ecs_view_read_t(itr, SceneScaleComp);

    const u64 id = (u64)ecs_view_entity(itr);

    switch (collision->type) {
    case SceneCollisionType_Sphere: {
      const GeoSphere sphere = scene_collision_world_sphere(&collision->sphere, trans, scale);
      geo_query_insert_sphere(env->queryEnv, sphere, id);
    } break;
    case SceneCollisionType_Capsule: {
      const GeoCapsule capsule = scene_collision_world_capsule(&collision->capsule, trans, scale);
      if (collision->capsule.height <= f32_epsilon) {
        const GeoSphere sphere = {.point = capsule.line.a, .radius = capsule.radius};
        geo_query_insert_sphere(env->queryEnv, sphere, id);
      } else {
        geo_query_insert_capsule(env->queryEnv, capsule, id);
      }
    } break;
    case SceneCollisionType_Box: {
      const GeoBoxRotated boxRotated = scene_collision_world_box(&collision->box, trans, scale);
      geo_query_insert_box_rotated(env->queryEnv, boxRotated, id);
    } break;
    default:
      diag_crash();
    }
  }
}

ecs_module_init(scene_collision_module) {
  ecs_register_comp(SceneCollisionEnvComp, .destructor = ecs_destruct_collision_env_comp);
  ecs_register_comp(SceneCollisionComp);

  ecs_register_view(CollisionEntityView);
  ecs_register_view(UpdateView);

  ecs_register_system(
      SceneCollisionUpdateSys, ecs_view_id(UpdateView), ecs_view_id(CollisionEntityView));

  ecs_order(SceneCollisionUpdateSys, SceneOrder_CollisionUpdate);
}

void scene_collision_add_sphere(
    EcsWorld* world, const EcsEntityId entity, const SceneCollisionSphere sphere) {
  ecs_world_add_t(
      world, entity, SceneCollisionComp, .type = SceneCollisionType_Sphere, .sphere = sphere);
}

void scene_collision_add_capsule(
    EcsWorld* world, const EcsEntityId entity, const SceneCollisionCapsule capsule) {
  ecs_world_add_t(
      world, entity, SceneCollisionComp, .type = SceneCollisionType_Capsule, .capsule = capsule);
}

void scene_collision_add_box(
    EcsWorld* world, const EcsEntityId entity, const SceneCollisionBox box) {
  ecs_world_add_t(world, entity, SceneCollisionComp, .type = SceneCollisionType_Box, .box = box);
}

bool scene_query_ray(const SceneCollisionEnvComp* env, const GeoRay* ray, SceneRayHit* out) {
  GeoQueryRayHit hit;
  if (geo_query_ray(env->queryEnv, ray, &hit)) {
    *out = (SceneRayHit){
        .entity   = (EcsEntityId)hit.shapeId,
        .position = geo_ray_position(ray, hit.time),
        .normal   = hit.normal,
        .time     = hit.time,
    };
    return true;
  }
  return false;
}

GeoSphere scene_collision_world_sphere(
    const SceneCollisionSphere* sphere,
    const SceneTransformComp*   trans,
    const SceneScaleComp*       scale) {

  const GeoVector basePos   = LIKELY(trans) ? trans->position : geo_vector(0);
  const GeoQuat   baseRot   = LIKELY(trans) ? trans->rotation : geo_quat_ident;
  const f32       baseScale = scale ? scale->scale : 1.0f;

  const GeoVector offset = geo_quat_rotate(baseRot, geo_vector_mul(sphere->offset, baseScale));
  const GeoVector point  = geo_vector_add(basePos, offset);

  return (GeoSphere){.point = point, .radius = sphere->radius * baseScale};
}

GeoCapsule scene_collision_world_capsule(
    const SceneCollisionCapsule* capsule,
    const SceneTransformComp*    trans,
    const SceneScaleComp*        scale) {

  const GeoVector basePos   = LIKELY(trans) ? trans->position : geo_vector(0);
  const GeoQuat   baseRot   = LIKELY(trans) ? trans->rotation : geo_quat_ident;
  const f32       baseScale = scale ? scale->scale : 1.0f;

  static const GeoVector g_capsuleDir[] = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}};

  const GeoVector offset = geo_quat_rotate(baseRot, geo_vector_mul(capsule->offset, baseScale));
  const GeoVector dir    = geo_quat_rotate(baseRot, g_capsuleDir[capsule->dir]);

  const GeoVector bottom = geo_vector_add(basePos, offset);
  const GeoVector top    = geo_vector_add(bottom, geo_vector_mul(dir, capsule->height * baseScale));

  return (GeoCapsule){.line = {bottom, top}, .radius = capsule->radius * baseScale};
}

GeoBoxRotated scene_collision_world_box(
    const SceneCollisionBox* box, const SceneTransformComp* trans, const SceneScaleComp* scale) {

  const GeoVector basePos   = LIKELY(trans) ? trans->position : geo_vector(0);
  const GeoQuat   baseRot   = LIKELY(trans) ? trans->rotation : geo_quat_ident;
  const f32       baseScale = scale ? scale->scale : 1.0f;

  const GeoVector localCenter = geo_vector_mul(geo_vector_add(box->min, box->max), baseScale * .5f);
  const GeoVector worldCenter = geo_vector_add(basePos, geo_quat_rotate(baseRot, localCenter));

  const GeoVector size = geo_vector_mul(geo_vector_sub(box->max, box->min), baseScale);
  return (GeoBoxRotated){
      .box      = geo_box_from_center(worldCenter, size),
      .rotation = baseRot,
  };
}

GeoBox scene_collision_world_bounds(
    const SceneCollisionComp* comp, const SceneTransformComp* trans, const SceneScaleComp* scale) {
  switch (comp->type) {
  case SceneCollisionType_Sphere: {
    const GeoSphere worldSphere = scene_collision_world_sphere(&comp->sphere, trans, scale);
    return geo_box_from_sphere(worldSphere.point, worldSphere.radius);
  }
  case SceneCollisionType_Capsule: {
    const GeoCapsule worldCapsule = scene_collision_world_capsule(&comp->capsule, trans, scale);
    return geo_box_from_capsule(worldCapsule.line.a, worldCapsule.line.b, worldCapsule.radius);
  }
  case SceneCollisionType_Box: {
    const GeoBox    localBox  = {.min = comp->box.min, .max = comp->box.max};
    const GeoVector basePos   = LIKELY(trans) ? trans->position : geo_vector(0);
    const GeoQuat   baseRot   = LIKELY(trans) ? trans->rotation : geo_quat_ident;
    const f32       baseScale = scale ? scale->scale : 1.0f;
    return geo_box_transform3(&localBox, basePos, baseRot, baseScale);
  }
  }
  UNREACHABLE
}
