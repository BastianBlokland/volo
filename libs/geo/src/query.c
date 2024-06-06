#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "core_thread.h"
#include "geo_box_rotated.h"
#include "geo_query.h"

#define geo_query_shape_align 16

ASSERT(alignof(GeoSphere) <= geo_query_shape_align, "Insufficient alignment")
ASSERT(alignof(GeoCapsule) <= geo_query_shape_align, "Insufficient alignment")
ASSERT(alignof(GeoBoxRotated) <= geo_query_shape_align, "Insufficient alignment")

typedef enum {
  QueryPrimType_Sphere,
  QueryPrimType_Capsule,
  QueryPrimType_BoxRotated,

  QueryPrimType_Count,
} QueryPrimType;

typedef struct {
  QueryPrimType  type;
  u32            count, capacity;
  u64*           ids;
  GeoQueryLayer* layers;
  GeoBox*        bounds;
  void*          shapes; // GeoSphere[] / GeoCapsule[] / GeoBoxRotated[]
} QueryPrim;

struct sGeoQueryEnv {
  Allocator* alloc;
  QueryPrim  prims[QueryPrimType_Count];
  i32        stats[GeoQueryStat_Count];
};

static usize prim_entry_size(const QueryPrimType type) {
  switch (type) {
  case QueryPrimType_Sphere:
    return sizeof(GeoSphere);
  case QueryPrimType_Capsule:
    return sizeof(GeoCapsule);
  case QueryPrimType_BoxRotated:
    return sizeof(GeoBoxRotated);
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static QueryPrim prim_create(const QueryPrimType type, const u32 capacity) {
  const usize shapeDataSize = prim_entry_size(type) * capacity;
  return (QueryPrim){
      .type     = type,
      .capacity = capacity,
      .ids      = alloc_array_t(g_allocHeap, u64, capacity),
      .layers   = alloc_array_t(g_allocHeap, GeoQueryLayer, capacity),
      .bounds   = alloc_array_t(g_allocHeap, GeoBox, capacity),
      .shapes   = alloc_alloc(g_allocHeap, shapeDataSize, geo_query_shape_align).ptr,
  };
}

static void prim_destroy(QueryPrim* p) {
  alloc_free_array_t(g_allocHeap, p->ids, p->capacity);
  alloc_free_array_t(g_allocHeap, p->layers, p->capacity);
  alloc_free_array_t(g_allocHeap, p->bounds, p->capacity);

  const Mem shapesMem = mem_create(p->shapes, prim_entry_size(p->type) * p->capacity);
  alloc_free(g_allocHeap, shapesMem);
}

static void prim_copy(QueryPrim* dst, const QueryPrim* src) {
  diag_assert(dst->type == src->type);
  diag_assert(dst->capacity >= src->count);

#define cpy_entry_field(_FIELD_, _SIZE_)                                                           \
  mem_cpy(                                                                                         \
      mem_create(dst->_FIELD_, (_SIZE_)*dst->capacity),                                            \
      mem_create(src->_FIELD_, (_SIZE_)*src->count))

  cpy_entry_field(ids, sizeof(u64));
  cpy_entry_field(layers, sizeof(GeoQueryLayer));
  cpy_entry_field(bounds, sizeof(GeoBox));
  cpy_entry_field(shapes, prim_entry_size(dst->type));

  dst->count = src->count;

#undef cpy_entry_field
}

NO_INLINE_HINT static void prim_grow(QueryPrim* p) {
  QueryPrim newPrim = prim_create(p->type, bits_nextpow2(p->capacity + 1));
  prim_copy(&newPrim, p);
  prim_destroy(p);
  *p = newPrim;
}

INLINE_HINT static void prim_ensure_next(QueryPrim* p) {
  if (LIKELY(p->capacity != p->count)) {
    return; // Enough space remaining.
  }
  prim_grow(p);
}

static f32 prim_intersect_ray(
    const QueryPrim* p, const u32 shapeIdx, const GeoRay* ray, GeoVector* outNormal) {
  switch (p->type) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)p->shapes)[shapeIdx];
    return geo_sphere_intersect_ray_info(sphere, ray, outNormal);
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* capsule = &((const GeoCapsule*)p->shapes)[shapeIdx];
    return geo_capsule_intersect_ray_info(capsule, ray, outNormal);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* boxRotated = &((const GeoBoxRotated*)p->shapes)[shapeIdx];
    return geo_box_rotated_intersect_ray_info(boxRotated, ray, outNormal);
  }
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static f32 prim_intersect_ray_fat(
    const QueryPrim* p,
    const u32        shapeIdx,
    const GeoRay*    ray,
    const f32        radius,
    GeoVector*       outNormal) {
  switch (p->type) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere        = &((const GeoSphere*)p->shapes)[shapeIdx];
    const GeoSphere  sphereDilated = geo_sphere_dilate(sphere, radius);
    const f32        hitT          = geo_sphere_intersect_ray(&sphereDilated, ray);
    if (hitT >= 0) {
      *outNormal = geo_vector_norm(geo_vector_sub(geo_ray_position(ray, hitT), sphere->point));
    }
    return hitT;
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* capsule        = &((const GeoCapsule*)p->shapes)[shapeIdx];
    const GeoCapsule  capsuleDilated = geo_capsule_dilate(capsule, radius);
    return geo_capsule_intersect_ray_info(&capsuleDilated, ray, outNormal);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* boxRotated = &((const GeoBoxRotated*)p->shapes)[shapeIdx];
    const GeoVector      dilateSize = geo_vector(radius, radius, radius);
    /**
     * Crude (conservative) estimation of a Minkowski-sum.
     * NOTE: Ignores the fact that the summed shape should have rounded corners, meaning we detect
     * intersections too early at the corners.
     */
    const GeoBoxRotated boxRotatedDilated = geo_box_rotated_dilate(boxRotated, dilateSize);
    return geo_box_rotated_intersect_ray_info(&boxRotatedDilated, ray, outNormal);
  }
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static bool prim_overlap_sphere(const QueryPrim* p, const u32 shapeIdx, const GeoSphere* shape) {
  switch (p->type) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)p->shapes)[shapeIdx];
    return geo_sphere_overlap(sphere, shape);
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* cap = &((const GeoCapsule*)p->shapes)[shapeIdx];
    return geo_capsule_overlap_sphere(cap, shape);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* box = &((const GeoBoxRotated*)p->shapes)[shapeIdx];
    return geo_box_rotated_overlap_sphere(box, shape);
  }
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static bool
prim_overlap_box_rotated(const QueryPrim* p, const u32 shapeIdx, const GeoBoxRotated* shape) {
  switch (p->type) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)p->shapes)[shapeIdx];
    return geo_box_rotated_overlap_sphere(shape, sphere);
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* cap = &((const GeoCapsule*)p->shapes)[shapeIdx];
    // TODO: Implement capsule <-> rotated-box overlap instead of converting capsules to boxes.
    const GeoBoxRotated box = geo_box_rotated_from_capsule(cap->line.a, cap->line.b, cap->radius);
    return geo_box_rotated_overlap_box_rotated(&box, shape);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* box = &((const GeoBoxRotated*)p->shapes)[shapeIdx];
    return geo_box_rotated_overlap_box_rotated(box, shape);
  }
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static bool prim_overlap_frustum(
    const QueryPrim* p, const u32 shapeIdx, const GeoVector frustum[PARAM_ARRAY_SIZE(8)]) {
  switch (p->type) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)p->shapes)[shapeIdx];
    return geo_sphere_overlap_frustum(sphere, frustum);
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* cap = &((const GeoCapsule*)p->shapes)[shapeIdx];
    // TODO: Implement capsule <-> frustum overlap instead of converting capsules to boxes.
    const GeoBoxRotated box = geo_box_rotated_from_capsule(cap->line.a, cap->line.b, cap->radius);
    return geo_box_rotated_overlap_frustum(&box, frustum);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* box = &((const GeoBoxRotated*)p->shapes)[shapeIdx];
    return geo_box_rotated_overlap_frustum(box, frustum);
  }
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static void query_validate_pos(MAYBE_UNUSED const GeoVector vec) {
  diag_assert_msg(
      geo_vector_mag_sqr(vec) <= (1e4f * 1e4f),
      "Position ({}) is out of bounds",
      geo_vector_fmt(vec));
}

static void query_validate_dir(MAYBE_UNUSED const GeoVector vec) {
  diag_assert_msg(
      math_abs(geo_vector_mag_sqr(vec) - 1.0f) <= 1e-5f,
      "Direction ({}) is not normalized",
      geo_vector_fmt(vec));
}

static bool query_filter_layer(const GeoQueryFilter* f, const GeoQueryLayer shapeLayer) {
  return (f->layerMask & shapeLayer) != 0;
}

static bool query_filter_callback(const GeoQueryFilter* f, const u64 shapeId, const u32 layer) {
  if (f->callback) {
    return f->callback(f->context, shapeId, layer);
  }
  return true;
}

static void query_stat_add(const GeoQueryEnv* env, const GeoQueryStat stat, const i32 value) {
  GeoQueryEnv* mutableEnv = (GeoQueryEnv*)env;
  thread_atomic_add_i32(&mutableEnv->stats[stat], value);
}

GeoQueryEnv* geo_query_env_create(Allocator* alloc) {
  GeoQueryEnv* env = alloc_alloc_t(alloc, GeoQueryEnv);

  *env = (GeoQueryEnv){
      .alloc                           = alloc,
      .prims[QueryPrimType_Sphere]     = prim_create(QueryPrimType_Sphere, 512),
      .prims[QueryPrimType_Capsule]    = prim_create(QueryPrimType_Capsule, 512),
      .prims[QueryPrimType_BoxRotated] = prim_create(QueryPrimType_BoxRotated, 512),
  };
  return env;
}

void geo_query_env_destroy(GeoQueryEnv* env) {
  array_for_t(env->prims, QueryPrim, prim) { prim_destroy(prim); }
  alloc_free_t(env->alloc, env);
}

void geo_query_env_clear(GeoQueryEnv* env) {
  array_for_t(env->prims, QueryPrim, prim) { prim->count = 0; }
}

void geo_query_insert_sphere(
    GeoQueryEnv* env, const GeoSphere sphere, const u64 id, const GeoQueryLayer layer) {
  query_validate_pos(sphere.point);
  diag_assert_msg(layer, "Shape needs at least one layer");

  QueryPrim* prim = &env->prims[QueryPrimType_Sphere];
  prim_ensure_next(prim);
  prim->ids[prim->count]                  = id;
  prim->layers[prim->count]               = layer;
  prim->bounds[prim->count]               = geo_box_from_sphere(sphere.point, sphere.radius);
  ((GeoSphere*)prim->shapes)[prim->count] = sphere;
  ++prim->count;
}

void geo_query_insert_capsule(
    GeoQueryEnv* env, const GeoCapsule capsule, const u64 id, const GeoQueryLayer layer) {
  query_validate_pos(capsule.line.a);
  query_validate_pos(capsule.line.b);
  diag_assert_msg(layer, "Shape needs at least one layer");

  QueryPrim* prim = &env->prims[QueryPrimType_Capsule];
  prim_ensure_next(prim);
  prim->ids[prim->count]    = id;
  prim->layers[prim->count] = layer;
  prim->bounds[prim->count] = geo_box_from_capsule(capsule.line.a, capsule.line.b, capsule.radius);
  ((GeoCapsule*)prim->shapes)[prim->count] = capsule;
  ++prim->count;
}

void geo_query_insert_box_rotated(
    GeoQueryEnv* env, const GeoBoxRotated box, const u64 id, const GeoQueryLayer layer) {
  query_validate_pos(box.box.min);
  query_validate_pos(box.box.max);
  diag_assert_msg(layer, "Shape needs at least one layer");

  QueryPrim* prim = &env->prims[QueryPrimType_BoxRotated];
  prim_ensure_next(prim);
  prim->ids[prim->count]                      = id;
  prim->layers[prim->count]                   = layer;
  prim->bounds[prim->count]                   = geo_box_from_rotated(&box.box, box.rotation);
  ((GeoBoxRotated*)prim->shapes)[prim->count] = box;
  ++prim->count;
}

bool geo_query_ray(
    const GeoQueryEnv*    env,
    const GeoRay*         ray,
    const f32             maxDist,
    const GeoQueryFilter* filter,
    GeoQueryRayHit*       outHit) {
  diag_assert(filter);
  diag_assert_msg(filter->layerMask, "Queries without any layers in the mask won't hit anything");
  diag_assert_msg(maxDist >= 0.0f, "Maximum raycast distance has to be positive");
  diag_assert_msg(maxDist <= 1e5f, "Maximum raycast distance ({}) exceeded", fmt_float(1e5f));
  query_validate_pos(ray->point);
  query_validate_dir(ray->dir);

  query_stat_add(env, GeoQueryStat_QueryRayCount, 1);

  const GeoLine queryLine = {
      .a = ray->point,
      .b = geo_vector_add(ray->point, geo_vector_mul(ray->dir, maxDist)),
  };
  const GeoBox queryBounds = geo_box_from_line(queryLine.a, queryLine.b);

  GeoQueryRayHit bestHit  = {.time = f32_max};
  bool           foundHit = false;

  for (u32 primIdx = 0; primIdx != QueryPrimType_Count; ++primIdx) {
    const QueryPrim* prim = &env->prims[primIdx];
    for (u32 shapeIdx = 0; shapeIdx != prim->count; ++shapeIdx) {
      if (!query_filter_layer(filter, prim->layers[shapeIdx])) {
        continue; // Layer not included in filter.
      }
      if (!geo_box_overlap(&prim->bounds[shapeIdx], &queryBounds)) {
        continue; // Bounds do not intersect; no need to test against the shape.
      }
      GeoVector normal;
      const f32 hitT = prim_intersect_ray(prim, shapeIdx, ray, &normal);
      if (hitT < 0.0 || hitT > maxDist) {
        continue; // Miss.
      }
      if (hitT >= bestHit.time) {
        continue; // Better hit already found.
      }
      if (!query_filter_callback(filter, prim->ids[shapeIdx], prim->layers[shapeIdx])) {
        continue; // Filtered out by the filter's callback.
      }

      // New best hit.
      bestHit.time    = hitT;
      bestHit.shapeId = prim->ids[shapeIdx];
      bestHit.normal  = normal;
      bestHit.layer   = prim->layers[shapeIdx];
      foundHit        = true;
    }
  }

  if (foundHit) {
    *outHit = bestHit;
  }
  return foundHit;
}

bool geo_query_ray_fat(
    const GeoQueryEnv*    env,
    const GeoRay*         ray,
    const f32             radius,
    const f32             maxDist,
    const GeoQueryFilter* filter,
    GeoQueryRayHit*       outHit) {
  diag_assert(filter);
  diag_assert_msg(filter->layerMask, "Queries without any layers in the mask won't hit anything");
  diag_assert_msg(radius >= 0.0f, "Raycast radius has to be positive");
  diag_assert_msg(maxDist >= 0.0f, "Maximum raycast distance has to be positive");
  diag_assert_msg(maxDist <= 1e5f, "Maximum raycast distance ({}) exceeded", fmt_float(1e5f));
  query_validate_pos(ray->point);
  query_validate_dir(ray->dir);

  query_stat_add(env, GeoQueryStat_QueryRayFatCount, 1);

  const GeoLine queryLine = {
      .a = ray->point,
      .b = geo_vector_add(ray->point, geo_vector_mul(ray->dir, maxDist)),
  };
  const GeoBox rayBox      = geo_box_from_line(queryLine.a, queryLine.b);
  const GeoBox queryBounds = geo_box_dilate(&rayBox, geo_vector(radius, radius, radius));

  GeoQueryRayHit bestHit  = {.time = f32_max};
  bool           foundHit = false;

  for (u32 primIdx = 0; primIdx != QueryPrimType_Count; ++primIdx) {
    const QueryPrim* prim = &env->prims[primIdx];
    for (u32 shapeIdx = 0; shapeIdx != prim->count; ++shapeIdx) {
      if (!query_filter_layer(filter, prim->layers[shapeIdx])) {
        continue; // Layer not included in filter.
      }
      if (!geo_box_overlap(&prim->bounds[shapeIdx], &queryBounds)) {
        continue; // Bounds do not intersect; no need to test against the shape.
      }
      GeoVector normal;
      const f32 hitT = prim_intersect_ray_fat(prim, shapeIdx, ray, radius, &normal);
      if (hitT < 0.0 || hitT > maxDist) {
        continue; // Miss.
      }
      if (hitT >= bestHit.time) {
        continue; // Better hit already found.
      }
      if (!query_filter_callback(filter, prim->ids[shapeIdx], prim->layers[shapeIdx])) {
        continue; // Filtered out by the filter's callback.
      }

      // New best hit.
      bestHit.time    = hitT;
      bestHit.shapeId = prim->ids[shapeIdx];
      bestHit.normal  = normal;
      bestHit.layer   = prim->layers[shapeIdx];
      foundHit        = true;
    }
  }

  if (foundHit) {
    *outHit = bestHit;
  }
  return foundHit;
}

u32 geo_query_sphere_all(
    const GeoQueryEnv*    env,
    const GeoSphere*      sphere,
    const GeoQueryFilter* filter,
    u64                   out[PARAM_ARRAY_SIZE(geo_query_max_hits)]) {

  query_stat_add(env, GeoQueryStat_QuerySphereAllCount, 1);

  const GeoBox queryBounds = geo_box_from_sphere(sphere->point, sphere->radius);

  u32 count = 0;
  for (u32 primIdx = 0; primIdx != QueryPrimType_Count; ++primIdx) {
    const QueryPrim* prim = &env->prims[primIdx];
    for (u32 shapeIdx = 0; shapeIdx != prim->count; ++shapeIdx) {
      if (!query_filter_layer(filter, prim->layers[shapeIdx])) {
        continue; // Layer not included in filter.
      }
      if (!geo_box_overlap(&prim->bounds[shapeIdx], &queryBounds)) {
        continue; // Bounds do not intersect; no need to test against the shape.
      }
      if (!prim_overlap_sphere(prim, shapeIdx, sphere)) {
        continue; // Miss.
      }
      if (!query_filter_callback(filter, prim->ids[shapeIdx], prim->layers[shapeIdx])) {
        continue; // Filtered out by the filter's callback.
      }

      // Output hit.
      out[count++] = prim->ids[shapeIdx];
      if (UNLIKELY(count == geo_query_max_hits)) {
        goto MaxCountReached;
      }
    }
  }

MaxCountReached:
  return count;
}

u32 geo_query_box_all(
    const GeoQueryEnv*    env,
    const GeoBoxRotated*  boxRotated,
    const GeoQueryFilter* filter,
    u64                   out[PARAM_ARRAY_SIZE(geo_query_max_hits)]) {

  query_stat_add(env, GeoQueryStat_QueryBoxAllCount, 1);

  const GeoBox queryBounds = geo_box_from_rotated(&boxRotated->box, boxRotated->rotation);

  u32 count = 0;
  for (u32 primIdx = 0; primIdx != QueryPrimType_Count; ++primIdx) {
    const QueryPrim* prim = &env->prims[primIdx];
    for (u32 shapeIdx = 0; shapeIdx != prim->count; ++shapeIdx) {
      if (!query_filter_layer(filter, prim->layers[shapeIdx])) {
        continue; // Layer not included in filter.
      }
      if (!geo_box_overlap(&prim->bounds[shapeIdx], &queryBounds)) {
        continue; // Bounds do not intersect; no need to test against the shape.
      }
      if (!prim_overlap_box_rotated(prim, shapeIdx, boxRotated)) {
        continue; // Miss.
      }
      if (!query_filter_callback(filter, prim->ids[shapeIdx], prim->layers[shapeIdx])) {
        continue; // Filtered out by the filter's callback.
      }

      // Output hit.
      out[count++] = prim->ids[shapeIdx];
      if (UNLIKELY(count == geo_query_max_hits)) {
        goto MaxCountReached;
      }
    }
  }

MaxCountReached:
  return count;
}

u32 geo_query_frustum_all(
    const GeoQueryEnv*    env,
    const GeoVector       frustum[PARAM_ARRAY_SIZE(8)],
    const GeoQueryFilter* filter,
    u64                   out[PARAM_ARRAY_SIZE(geo_query_max_hits)]) {

  query_stat_add(env, GeoQueryStat_QueryFrustumAllCount, 1);

  const GeoBox queryBounds = geo_box_from_frustum(frustum);

  u32 count = 0;
  for (u32 primIdx = 0; primIdx != QueryPrimType_Count; ++primIdx) {
    const QueryPrim* prim = &env->prims[primIdx];
    for (u32 shapeIdx = 0; shapeIdx != prim->count; ++shapeIdx) {
      if (!query_filter_layer(filter, prim->layers[shapeIdx])) {
        continue; // Layer not included in filter.
      }
      if (!geo_box_overlap(&prim->bounds[shapeIdx], &queryBounds)) {
        continue; // Bounds do not intersect; no need to test against the shape.
      }
      if (!prim_overlap_frustum(prim, shapeIdx, frustum)) {
        continue; // Miss.
      }
      if (!query_filter_callback(filter, prim->ids[shapeIdx], prim->layers[shapeIdx])) {
        continue; // Filtered out by the filter's callback.
      }

      // Output hit.
      out[count++] = prim->ids[shapeIdx];
      if (UNLIKELY(count == geo_query_max_hits)) {
        goto MaxCountReached;
      }
    }
  }

MaxCountReached:
  return count;
}

void geo_query_stats_reset(GeoQueryEnv* env) { mem_set(array_mem(env->stats), 0); }

i32* geo_query_stats(GeoQueryEnv* env) {
  env->stats[GeoQueryStat_PrimSphereCount]     = (i32)env->prims[QueryPrimType_Sphere].count;
  env->stats[GeoQueryStat_PrimCapsuleCount]    = (i32)env->prims[QueryPrimType_Capsule].count;
  env->stats[GeoQueryStat_PrimBoxRotatedCount] = (i32)env->prims[QueryPrimType_BoxRotated].count;
  return env->stats;
}
