#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "geo_box_rotated.h"
#include "geo_capsule.h"
#include "geo_query.h"
#include "geo_sphere.h"
#include "scene.h"
#include "scene_collision.h"
#include "scene_register.h"
#include "scene_transform.h"
#include "trace_tracer.h"

ASSERT(sizeof(EcsEntityId) == sizeof(u64), "EntityId's have to be interpretable as 64bit integers");
ASSERT(geo_query_max_hits == scene_query_max_hits, "Mismatching maximum query hits");
ASSERT(scene_query_stat_count == GeoQueryStat_Count, "Mismatching collision query stat count");

ecs_comp_define(SceneCollisionEnvComp) {
  SceneLayer   ignoreMask; // Layers to ignore globally.
  GeoQueryEnv* queryEnv;
};
ecs_comp_define_public(SceneCollisionStatsComp);
ecs_comp_define_public(SceneCollisionComp);

static void ecs_destruct_collision_env_comp(void* data) {
  SceneCollisionEnvComp* env = data;
  geo_query_env_destroy(env->queryEnv);
}

static void ecs_destruct_collision(void* data) {
  SceneCollisionComp* comp = data;
  alloc_free_array_t(g_allocHeap, comp->shapes, comp->shapeCount);
}

ecs_view_define(InitGlobalView) { ecs_access_write(SceneCollisionEnvComp); }

ecs_view_define(CollisionView) {
  ecs_access_read(SceneCollisionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
}

ecs_view_define(TransformView) { ecs_access_read(SceneTransformComp); }

static void collision_env_create(EcsWorld* world) {
  GeoQueryEnv* queryEnv = geo_query_env_create(g_allocHeap);

  ecs_world_add_t(world, ecs_world_global(world), SceneCollisionEnvComp, .queryEnv = queryEnv);
  ecs_world_add_t(world, ecs_world_global(world), SceneCollisionStatsComp);
}

static void scene_collision_stats_update(SceneCollisionStatsComp* stats, GeoQueryEnv* queryEnv) {
  const i32* statsPtr = geo_query_stats(queryEnv);

  // Copy the query stats into the stats component.
  mem_cpy(array_mem(stats->queryStats), mem_create(statsPtr, sizeof(i32) * GeoQueryStat_Count));

  geo_query_stats_reset(queryEnv);
}

ecs_system_define(SceneCollisionInitSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SceneCollisionEnvComp)) {
    collision_env_create(world);
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, InitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  EcsView* collisionView = ecs_world_view_t(world, CollisionView);
  EcsView* transformView = ecs_world_view_t(world, TransformView);

  SceneCollisionEnvComp* env = ecs_view_write_t(globalItr, SceneCollisionEnvComp);
  geo_query_env_clear(env->queryEnv);

  /**
   * Insert geo shapes for all colliders.
   */
  trace_begin("collision_insert", TraceColor_Blue);
  for (EcsIterator* itr = ecs_view_itr(collisionView); ecs_view_walk(itr);) {
    const SceneCollisionComp* collision = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale     = ecs_view_read_t(itr, SceneScaleComp);

    diag_assert_msg(collision->layer, "SceneCollision needs at least one layer");
    if (collision->layer & env->ignoreMask) {
      continue;
    }

    const u64           userId     = (u64)ecs_view_entity(itr);
    const GeoQueryLayer queryLayer = (GeoQueryLayer)collision->layer;
    for (u32 i = 0; i != collision->shapeCount; ++i) {
      const SceneCollisionShape* shapeLocal = &collision->shapes[i];
      const SceneCollisionShape  shape      = scene_collision_shape_world(shapeLocal, trans, scale);
      switch (shape.type) {
      case SceneCollisionType_Sphere:
        geo_query_insert_sphere(env->queryEnv, shape.sphere, userId, queryLayer);
        break;
      case SceneCollisionType_Capsule: {
        if (geo_line_length_sqr(&shape.capsule.line) <= 1e-2f) {
          const GeoSphere sphere = {.point = shape.capsule.line.a, .radius = shape.capsule.radius};
          geo_query_insert_sphere(env->queryEnv, sphere, userId, queryLayer);
        } else {
          geo_query_insert_capsule(env->queryEnv, shape.capsule, userId, queryLayer);
        }
      } break;
      case SceneCollisionType_Box:
        geo_query_insert_box_rotated(env->queryEnv, shape.box, userId, queryLayer);
        break;
      default:
        UNREACHABLE
      }
    }
  }

  /**
   * Insert a debug sphere shape for all entities with a transform.
   * The debug shapes are useful to be able to select entities without a collider.
   */
  if (!(env->ignoreMask & SceneLayer_Debug)) {
    for (EcsIterator* itr = ecs_view_itr(transformView); ecs_view_walk(itr);) {
      const EcsEntityId e = ecs_view_entity(itr);
      if (ecs_world_has_t(world, e, SceneCameraComp)) {
        // NOTE: Hacky but we want to avoid the camera having collision as it will block queries.
        continue;
      }
      const SceneTransformComp* trans  = ecs_view_read_t(itr, SceneTransformComp);
      const GeoSphere           sphere = {.point = trans->position, .radius = 0.25f};
      geo_query_insert_sphere(env->queryEnv, sphere, (u64)e, (GeoQueryLayer)SceneLayer_Debug);
    }
  }
  trace_end();

  /**
   * Build the query.
   */
  trace_begin("collision_build", TraceColor_Blue);
  geo_query_build(env->queryEnv);
  trace_end();
}

ecs_view_define(StatsGlobalView) {
  ecs_access_write(SceneCollisionEnvComp);
  ecs_access_write(SceneCollisionStatsComp);
}

ecs_system_define(SceneCollisionStatsSys) {
  EcsView*     globalView = ecs_world_view_t(world, StatsGlobalView);
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
  ecs_register_comp(SceneCollisionComp, .destructor = ecs_destruct_collision);

  ecs_register_view(InitGlobalView);
  ecs_register_view(CollisionView);
  ecs_register_view(TransformView);

  ecs_register_system(
      SceneCollisionInitSys,
      ecs_view_id(InitGlobalView),
      ecs_view_id(CollisionView),
      ecs_view_id(TransformView));

  ecs_order(SceneCollisionInitSys, SceneOrder_CollisionInit);

  ecs_register_system(SceneCollisionStatsSys, ecs_register_view(StatsGlobalView));

  enum {
    SceneOrder_Normal         = 0,
    SceneOrder_CollisionStats = 1,
  };
  ecs_order(SceneCollisionStatsSys, SceneOrder_CollisionStats);
}

String scene_layer_name(const SceneLayer layer) {
  diag_assert_msg(bits_popcnt((u32)layer) == 1, "Exactly one layer should be enabled");
  const u32           index     = bits_ctz_32(layer);
  static const String g_names[] = {
      string_static("Debug"),
      string_static("Environment"),
      string_static("InfantryFactionA"),
      string_static("InfantryFactionB"),
      string_static("InfantryFactionC"),
      string_static("InfantryFactionD"),
      string_static("InfantryFactionNone"),
      string_static("VehicleFactionA"),
      string_static("VehicleFactionB"),
      string_static("VehicleFactionC"),
      string_static("VehicleFactionD"),
      string_static("VehicleFactionNone"),
      string_static("StructureFactionA"),
      string_static("StructureFactionB"),
      string_static("StructureFactionC"),
      string_static("StructureFactionD"),
      string_static("StructureFactionNone"),
      string_static("Destructible"),
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

SceneLayer scene_collision_ignore_mask(const SceneCollisionEnvComp* env) { return env->ignoreMask; }

void scene_collision_ignore_mask_set(SceneCollisionEnvComp* env, const SceneLayer mask) {
  env->ignoreMask = mask;
}

SceneCollisionComp* scene_collision_add(
    EcsWorld*                 world,
    const EcsEntityId         entity,
    const SceneCollisionShape shape,
    const SceneLayer          layer) {
  diag_assert_msg(bits_popcnt((u32)layer) == 1, "Collider can only be in 1 layer");

  SceneCollisionComp* comp = ecs_world_add_t(world, entity, SceneCollisionComp, .layer = layer);

  comp->shapeCount = 1;
  comp->shapes     = alloc_array_t(g_allocHeap, SceneCollisionShape, comp->shapeCount);

  comp->shapes[0] = shape;

  return comp;
}

f32 scene_collision_shape_intersect_ray(
    const SceneCollisionShape* shape,
    const SceneTransformComp*  trans,
    const SceneScaleComp*      scale,
    const GeoRay*              ray) {

  const SceneCollisionShape world = scene_collision_shape_world(shape, trans, scale);
  switch (world.type) {
  case SceneCollisionType_Sphere:
    return geo_sphere_intersect_ray(&world.sphere, ray);
  case SceneCollisionType_Capsule:
    return geo_capsule_intersect_ray(&world.capsule, ray);
  case SceneCollisionType_Box:
    return geo_box_rotated_intersect_ray(&world.box, ray);
  default:
    break;
  }
  UNREACHABLE
}

f32 scene_collision_intersect_ray(
    const SceneCollisionComp* collision,
    const SceneTransformComp* trans,
    const SceneScaleComp*     scale,
    const GeoRay*             ray) {

  f32  dist = f32_max;
  bool hit  = false;
  for (u32 i = 0; i != collision->shapeCount; ++i) {
    const SceneCollisionShape* shape = &collision->shapes[i];
    const f32 shapeDist = scene_collision_shape_intersect_ray(shape, trans, scale, ray);

    if (shapeDist >= 0.0f && shapeDist < dist) {
      dist = shapeDist;
      hit  = true;
    }
  }
  return hit ? dist : -1.0f;
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
        .entity   = (EcsEntityId)hit.userId,
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
        .time     = hit.time,
        .entity   = (EcsEntityId)hit.userId,
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
    EcsEntityId                  out[PARAM_ARRAY_SIZE(geo_query_max_hits)]) {
  diag_assert(filter);

  const GeoQueryFilter geoFilter = {
      .context   = filter->context,
      .callback  = filter->callback,
      .layerMask = (GeoQueryLayer)filter->layerMask,
  };
  return geo_query_sphere_all(env->queryEnv, sphere, &geoFilter, (u64*)out);
}

u32 scene_query_box_all(
    const SceneCollisionEnvComp* env,
    const GeoBoxRotated*         box,
    const SceneQueryFilter*      filter,
    EcsEntityId                  out[PARAM_ARRAY_SIZE(geo_query_max_hits)]) {
  diag_assert(filter);

  const GeoQueryFilter geoFilter = {
      .context   = filter->context,
      .callback  = filter->callback,
      .layerMask = (GeoQueryLayer)filter->layerMask,
  };
  return geo_query_box_all(env->queryEnv, box, &geoFilter, (u64*)out);
}

u32 scene_query_frustum_all(
    const SceneCollisionEnvComp* env,
    const GeoVector              frustum[PARAM_ARRAY_SIZE(8)],
    const SceneQueryFilter*      filter,
    EcsEntityId                  out[PARAM_ARRAY_SIZE(geo_query_max_hits)]) {
  diag_assert(filter);

  const GeoQueryFilter geoFilter = {
      .context   = filter->context,
      .callback  = filter->callback,
      .layerMask = (GeoQueryLayer)filter->layerMask,
  };
  return geo_query_frustum_all(env->queryEnv, frustum, &geoFilter, (u64*)out);
}

SceneCollisionShape scene_collision_shape_world(
    const SceneCollisionShape* shape,
    const SceneTransformComp*  trans,
    const SceneScaleComp*      scale) {
  const GeoVector basePos   = trans->position;
  const GeoQuat   baseRot   = trans->rotation;
  const f32       baseScale = scale ? scale->scale : 1.0f;

  SceneCollisionShape res;
  res.type = shape->type;

  switch (shape->type) {
  case SceneCollisionType_Sphere:
    res.sphere = geo_sphere_transform3(&shape->sphere, basePos, baseRot, baseScale);
    return res;
  case SceneCollisionType_Capsule:
    res.capsule = geo_capsule_transform3(&shape->capsule, basePos, baseRot, baseScale);
    return res;
  case SceneCollisionType_Box:
    res.box = geo_box_rotated_transform3(&shape->box, basePos, baseRot, baseScale);
    return res;
  case SceneCollisionType_Count:
    break;
  }
  UNREACHABLE
}

GeoBox scene_collision_shape_bounds(
    const SceneCollisionShape* shape,
    const SceneTransformComp*  trans,
    const SceneScaleComp*      scale) {

  const SceneCollisionShape world = scene_collision_shape_world(shape, trans, scale);
  switch (world.type) {
  case SceneCollisionType_Sphere:
    return geo_box_from_sphere(world.sphere.point, world.sphere.radius);
  case SceneCollisionType_Capsule:
    return geo_box_from_capsule(world.capsule.line.a, world.capsule.line.b, world.capsule.radius);
  case SceneCollisionType_Box:
    return geo_box_from_rotated(&world.box.box, world.box.rotation);
  case SceneCollisionType_Count:
    break;
  }
  UNREACHABLE
}

GeoBox scene_collision_bounds(
    const SceneCollisionComp* comp, const SceneTransformComp* trans, const SceneScaleComp* scale) {
  diag_assert(comp->shapeCount);

  GeoBox result = scene_collision_shape_bounds(&comp->shapes[0], trans, scale);
  for (u32 i = 1; i != comp->shapeCount; ++i) {
    const GeoBox shapeBounds = scene_collision_shape_bounds(&comp->shapes[i], trans, scale);
    result                   = geo_box_encapsulate_box(&result, &shapeBounds);
  }
  return result;
}

const GeoQueryEnv* scene_collision_query_env(const SceneCollisionEnvComp* env) {
  return env->queryEnv;
}
