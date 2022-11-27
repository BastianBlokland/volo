#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "geo_query.h"
#include "scene_collision.h"
#include "scene_register.h"
#include "scene_transform.h"

ASSERT(sizeof(EcsEntityId) == sizeof(u64), "EntityId's have to be interpretable as 64bit integers");
ASSERT(geo_query_max_hits == scene_query_max_hits, "Mismatching maximum query hits");
ASSERT(scene_query_stat_count == GeoQueryStat_Count, "Mismatching collision query stat count");

ecs_comp_define(SceneCollisionEnvComp) { GeoQueryEnv* queryEnv; };
ecs_comp_define_public(SceneCollisionStatsComp);
ecs_comp_define_public(SceneCollisionComp);

static void ecs_destruct_collision_env_comp(void* data) {
  SceneCollisionEnvComp* env = data;
  geo_query_env_destroy(env->queryEnv);
}

ecs_view_define(UpdateGlobalView) { ecs_access_write(SceneCollisionEnvComp); }

ecs_view_define(CollisionEntityView) {
  ecs_access_read(SceneCollisionComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
}

static void collision_env_create(EcsWorld* world) {
  GeoQueryEnv* queryEnv = geo_query_env_create(g_alloc_heap);

  ecs_world_add_t(world, ecs_world_global(world), SceneCollisionEnvComp, .queryEnv = queryEnv);
  ecs_world_add_t(world, ecs_world_global(world), SceneCollisionStatsComp);
}

static void scene_collision_stats_update(SceneCollisionStatsComp* stats, GeoQueryEnv* queryEnv) {
  const i32* statsPtr = geo_query_stats(queryEnv);

  // Copy the query stats into the stats component.
  mem_cpy(array_mem(stats->queryStats), mem_create(statsPtr, sizeof(i32) * GeoQueryStat_Count));

  geo_query_stats_reset(queryEnv);
}

ecs_system_define(SceneCollisionUpdateSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SceneCollisionEnvComp)) {
    collision_env_create(world);
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  SceneCollisionEnvComp* env = ecs_view_write_t(globalItr, SceneCollisionEnvComp);
  geo_query_env_clear(env->queryEnv);

  EcsView* collisionEntities = ecs_world_view_t(world, CollisionEntityView);
  for (EcsIterator* itr = ecs_view_itr(collisionEntities); ecs_view_walk(itr);) {
    const SceneCollisionComp* collision = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale     = ecs_view_read_t(itr, SceneScaleComp);

    diag_assert_msg(collision->layer, "SceneCollision needs at least one layer");

    const u64           id         = (u64)ecs_view_entity(itr);
    const GeoQueryLayer queryLayer = (GeoQueryLayer)collision->layer;

    switch (collision->type) {
    case SceneCollisionType_Sphere: {
      const GeoSphere sphere = scene_collision_world_sphere(&collision->sphere, trans, scale);
      geo_query_insert_sphere(env->queryEnv, sphere, id, queryLayer);
    } break;
    case SceneCollisionType_Capsule: {
      const GeoCapsule capsule = scene_collision_world_capsule(&collision->capsule, trans, scale);
      if (collision->capsule.height <= f32_epsilon) {
        const GeoSphere sphere = {.point = capsule.line.a, .radius = capsule.radius};
        geo_query_insert_sphere(env->queryEnv, sphere, id, queryLayer);
      } else {
        geo_query_insert_capsule(env->queryEnv, capsule, id, queryLayer);
      }
    } break;
    case SceneCollisionType_Box: {
      const GeoBoxRotated boxRotated = scene_collision_world_box(&collision->box, trans, scale);
      geo_query_insert_box_rotated(env->queryEnv, boxRotated, id, queryLayer);
    } break;
    default:
      diag_crash();
    }
  }
}

ecs_view_define(UpdateStatsGlobalView) {
  ecs_access_write(SceneCollisionEnvComp);
  ecs_access_write(SceneCollisionStatsComp);
}

ecs_system_define(SceneCollisionUpdateStatsSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateStatsGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneCollisionEnvComp*   env   = ecs_view_write_t(globalItr, SceneCollisionEnvComp);
  SceneCollisionStatsComp* stats = ecs_view_write_t(globalItr, SceneCollisionStatsComp);

  scene_collision_stats_update(stats, env->queryEnv);
}

ecs_module_init(scene_collision_module) {
  ecs_register_comp(SceneCollisionEnvComp, .destructor = ecs_destruct_collision_env_comp);
  ecs_register_comp(SceneCollisionStatsComp);
  ecs_register_comp(SceneCollisionComp);

  ecs_register_view(UpdateGlobalView);
  ecs_register_view(CollisionEntityView);

  ecs_register_system(
      SceneCollisionUpdateSys, ecs_view_id(UpdateGlobalView), ecs_view_id(CollisionEntityView));

  ecs_order(SceneCollisionUpdateSys, SceneOrder_CollisionUpdate);

  ecs_register_system(SceneCollisionUpdateStatsSys, ecs_register_view(UpdateStatsGlobalView));

  enum {
    SceneOrder_Normal               = 0,
    SceneOrder_CollisionStatsUpdate = 1,
  };
  ecs_order(SceneCollisionUpdateStatsSys, SceneOrder_CollisionStatsUpdate);
}

String scene_layer_name(const SceneLayer layer) {
  diag_assert_msg(bits_popcnt((u32)layer) == 1, "Exactly one layer should be enabled");
  const u32           index     = bits_ctz_32(layer);
  static const String g_names[] = {
      string_static("Debug"),
      string_static("Environment"),
      string_static("UnitFactionA"),
      string_static("UnitFactionB"),
      string_static("UnitFactionC"),
      string_static("UnitFactionD"),
      string_static("UnitFactionNone"),
  };
  ASSERT(array_elems(g_names) == SceneLayer_Count, "Incorrect number of layer names");
  return g_names[index];
}

String scene_collision_type_name(const SceneCollisionType type) {
  diag_assert(type < SceneCollisionType_Count);
  static const String g_names[] = {
      string_static("Sphere"),
      string_static("Capsule"),
      string_static("Box"),
  };
  ASSERT(array_elems(g_names) == SceneCollisionType_Count, "Incorrect number of type names");
  return g_names[type];
}

void scene_collision_add_sphere(
    EcsWorld*                  world,
    const EcsEntityId          entity,
    const SceneCollisionSphere sphere,
    const SceneLayer           layer) {

  ecs_world_add_t(
      world,
      entity,
      SceneCollisionComp,
      .type   = SceneCollisionType_Sphere,
      .layer  = layer,
      .sphere = sphere);
}

void scene_collision_add_capsule(
    EcsWorld*                   world,
    const EcsEntityId           entity,
    const SceneCollisionCapsule capsule,
    const SceneLayer            layer) {

  ecs_world_add_t(
      world,
      entity,
      SceneCollisionComp,
      .type    = SceneCollisionType_Capsule,
      .layer   = layer,
      .capsule = capsule);
}

void scene_collision_add_box(
    EcsWorld*               world,
    const EcsEntityId       entity,
    const SceneCollisionBox box,
    const SceneLayer        layer) {

  ecs_world_add_t(
      world,
      entity,
      SceneCollisionComp,
      .type  = SceneCollisionType_Box,
      .layer = layer,
      .box   = box);
}

bool scene_query_ray(
    const SceneCollisionEnvComp* env,
    const GeoRay*                ray,
    const f32                    maxDist,
    const SceneQueryFilter*      filter,
    SceneRayHit*                 out) {
  diag_assert(filter);

  GeoQueryRayHit       hit;
  const GeoQueryFilter geoFilter = {
      .context   = filter->context,
      .callback  = filter->callback,
      .layerMask = (GeoQueryLayer)filter->layerMask,
  };
  if (geo_query_ray(env->queryEnv, ray, maxDist, &geoFilter, &hit)) {
    *out = (SceneRayHit){
        .time     = hit.time,
        .entity   = (EcsEntityId)hit.shapeId,
        .position = geo_ray_position(ray, hit.time),
        .normal   = hit.normal,
        .layer    = (SceneLayer)hit.layer,
    };
    return true;
  }
  return false;
}

bool scene_query_ray_fat(
    const SceneCollisionEnvComp* env,
    const GeoRay*                ray,
    const f32                    radius,
    const f32                    maxDist,
    const SceneQueryFilter*      filter,
    SceneRayHit*                 out) {
  diag_assert(filter);

  GeoQueryRayHit       hit;
  const GeoQueryFilter geoFilter = {
      .context   = filter->context,
      .callback  = filter->callback,
      .layerMask = (GeoQueryLayer)filter->layerMask,
  };
  if (geo_query_ray_fat(env->queryEnv, ray, radius, maxDist, &geoFilter, &hit)) {
    *out = (SceneRayHit){
        .time   = hit.time,
        .entity = (EcsEntityId)hit.shapeId,
        /**
         * TODO: Instead of always outputting positions on the ray we should find the actual
         * intersection point.
         */
        .position = geo_ray_position(ray, hit.time),
        .normal   = hit.normal,
        .layer    = (SceneLayer)hit.layer,
    };
    return true;
  }
  return false;
}

u32 scene_query_sphere_all(
    const SceneCollisionEnvComp* env,
    const GeoSphere*             sphere,
    const SceneQueryFilter*      filter,
    EcsEntityId                  out[geo_query_max_hits]) {
  diag_assert(filter);

  const GeoQueryFilter geoFilter = {
      .context   = filter->context,
      .callback  = filter->callback,
      .layerMask = (GeoQueryLayer)filter->layerMask,
  };
  return geo_query_sphere_all(env->queryEnv, sphere, &geoFilter, (u64*)out);
}

u32 scene_query_frustum_all(
    const SceneCollisionEnvComp* env,
    const GeoVector              frustum[8],
    const SceneQueryFilter*      filter,
    EcsEntityId                  out[geo_query_max_hits]) {
  diag_assert(filter);

  const GeoQueryFilter geoFilter = {
      .context   = filter->context,
      .callback  = filter->callback,
      .layerMask = (GeoQueryLayer)filter->layerMask,
  };
  return geo_query_frustum_all(env->queryEnv, frustum, &geoFilter, (u64*)out);
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
  case SceneCollisionType_Count:
    break;
  }
  UNREACHABLE
}
