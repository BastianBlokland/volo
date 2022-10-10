#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "geo_box_rotated.h"
#include "geo_query.h"

#define geo_query_shape_align 16

ASSERT(alignof(GeoSphere) <= geo_query_shape_align, "Insufficient alignment")
ASSERT(alignof(GeoCapsule) <= geo_query_shape_align, "Insufficient alignment")
ASSERT(alignof(GeoBoxRotated) <= geo_query_shape_align, "Insufficient alignment")

typedef enum {
  GeoQueryPrim_Sphere,
  GeoQueryPrim_Capsule,
  GeoQueryPrim_BoxRotated,

  GeoQueryPrim_Count,
} GeoQueryPrimType;

typedef struct {
  GeoQueryPrimType type;
  u32              count, capacity;
  u64*             ids;
  GeoQueryLayer*   layers;
  void*            shapes; // GeoSphere[] / GeoCapsule[] / GeoBoxRotated[]
} GeoQueryPrim;

struct sGeoQueryEnv {
  Allocator*   alloc;
  GeoQueryPrim prims[GeoQueryPrim_Count];
};

static usize geo_prim_entry_size(const GeoQueryPrimType type) {
  switch (type) {
  case GeoQueryPrim_Sphere:
    return sizeof(GeoSphere);
  case GeoQueryPrim_Capsule:
    return sizeof(GeoCapsule);
  case GeoQueryPrim_BoxRotated:
    return sizeof(GeoBoxRotated);
  case GeoQueryPrim_Count:
    break;
  }
  UNREACHABLE
}

static GeoQueryPrim geo_prim_create(const GeoQueryPrimType type, const u32 capacity) {
  const usize shapeDataSize = geo_prim_entry_size(type) * capacity;
  return (GeoQueryPrim){
      .type     = type,
      .capacity = capacity,
      .ids      = alloc_array_t(g_alloc_heap, u64, capacity),
      .layers   = alloc_array_t(g_alloc_heap, GeoQueryLayer, capacity),
      .shapes   = alloc_alloc(g_alloc_heap, shapeDataSize, geo_query_shape_align).ptr,
  };
}

static void geo_prim_destroy(GeoQueryPrim* prim) {
  alloc_free_array_t(g_alloc_heap, prim->ids, prim->capacity);
  alloc_free_array_t(g_alloc_heap, prim->layers, prim->capacity);
  const Mem shapesMem = mem_create(prim->shapes, geo_prim_entry_size(prim->type) * prim->capacity);
  alloc_free(g_alloc_heap, shapesMem);
}

static void geo_prim_copy(GeoQueryPrim* dst, const GeoQueryPrim* src) {
  diag_assert(dst->type == src->type);
  diag_assert(dst->capacity >= src->count);

  mem_cpy(
      mem_create(dst->ids, sizeof(u64) * dst->capacity),
      mem_create(src->ids, sizeof(u64) * src->count));

  mem_cpy(
      mem_create(dst->layers, sizeof(GeoQueryLayer) * dst->capacity),
      mem_create(src->layers, sizeof(GeoQueryLayer) * src->count));

  const usize shapeEntrySize = geo_prim_entry_size(dst->type);
  mem_cpy(
      mem_create(dst->shapes, shapeEntrySize * dst->capacity),
      mem_create(src->shapes, shapeEntrySize * src->count));

  dst->count = src->count;
}

static void geo_prim_ensure_next(GeoQueryPrim* prim) {
  if (LIKELY(prim->capacity != prim->count)) {
    return; // Enough space remaining.
  }
  GeoQueryPrim newPrim = geo_prim_create(prim->type, bits_nextpow2(prim->capacity + 1));
  geo_prim_copy(&newPrim, prim);
  geo_prim_destroy(prim);
  *prim = newPrim;
}

static void geo_query_validate_pos(MAYBE_UNUSED const GeoVector vec) {
  // Constrain the positions 1000 meters from the origin to avoid precision issues.
  diag_assert_msg(
      geo_vector_mag_sqr(vec) <= (1e4f * 1e4f),
      "Position ({}) is out of bounds",
      geo_vector_fmt(vec));
}

static void geo_query_validate_dir(MAYBE_UNUSED const GeoVector vec) {
  diag_assert_msg(
      math_abs(geo_vector_mag_sqr(vec) - 1.0f) <= 1e-6f,
      "Direction ({}) is not normalized",
      geo_vector_fmt(vec));
  return;
}

static bool
geo_query_filter(const GeoQueryFilter* filter, const u64 shapeId, const GeoQueryLayer shapeLayer) {
  if ((filter->layerMask & shapeLayer) == 0) {
    return false;
  }
  if (filter->callback) {
    return filter->callback(filter->context, shapeId);
  }
  return true;
}

GeoQueryEnv* geo_query_env_create(Allocator* alloc) {
  GeoQueryEnv* env = alloc_alloc_t(alloc, GeoQueryEnv);

  *env = (GeoQueryEnv){
      .alloc                          = alloc,
      .prims[GeoQueryPrim_Sphere]     = geo_prim_create(GeoQueryPrim_Sphere, 512),
      .prims[GeoQueryPrim_Capsule]    = geo_prim_create(GeoQueryPrim_Capsule, 512),
      .prims[GeoQueryPrim_BoxRotated] = geo_prim_create(GeoQueryPrim_BoxRotated, 512),
  };
  return env;
}

void geo_query_env_destroy(GeoQueryEnv* env) {
  array_for_t(env->prims, GeoQueryPrim, prim) { geo_prim_destroy(prim); }
  alloc_free_t(env->alloc, env);
}

void geo_query_env_clear(GeoQueryEnv* env) {
  array_for_t(env->prims, GeoQueryPrim, prim) { prim->count = 0; }
}

void geo_query_insert_sphere(
    GeoQueryEnv* env, const GeoSphere sphere, const u64 id, const GeoQueryLayer layer) {
  geo_query_validate_pos(sphere.point);
  diag_assert_msg(layer, "Shape needs at least one layer");

  GeoQueryPrim* prim = &env->prims[GeoQueryPrim_Sphere];
  geo_prim_ensure_next(prim);
  prim->ids[prim->count]                  = id;
  prim->layers[prim->count]               = layer;
  ((GeoSphere*)prim->shapes)[prim->count] = sphere;
  ++prim->count;
}

void geo_query_insert_capsule(
    GeoQueryEnv* env, const GeoCapsule capsule, const u64 id, const GeoQueryLayer layer) {
  geo_query_validate_pos(capsule.line.a);
  geo_query_validate_pos(capsule.line.b);
  diag_assert_msg(layer, "Shape needs at least one layer");

  GeoQueryPrim* prim = &env->prims[GeoQueryPrim_Capsule];
  geo_prim_ensure_next(prim);
  prim->ids[prim->count]                   = id;
  prim->layers[prim->count]                = layer;
  ((GeoCapsule*)prim->shapes)[prim->count] = capsule;
  ++prim->count;
}

void geo_query_insert_box_rotated(
    GeoQueryEnv* env, const GeoBoxRotated box, const u64 id, const GeoQueryLayer layer) {
  geo_query_validate_pos(box.box.min);
  geo_query_validate_pos(box.box.max);
  diag_assert_msg(layer, "Shape needs at least one layer");

  GeoQueryPrim* prim = &env->prims[GeoQueryPrim_BoxRotated];
  geo_prim_ensure_next(prim);
  prim->ids[prim->count]                      = id;
  prim->layers[prim->count]                   = layer;
  ((GeoBoxRotated*)prim->shapes)[prim->count] = box;
  ++prim->count;
}

bool geo_query_ray(
    const GeoQueryEnv*    env,
    const GeoRay*         ray,
    const GeoQueryFilter* filter,
    GeoQueryRayHit*       outHit) {
  diag_assert(filter);
  diag_assert_msg(filter->layerMask, "Queries without any layers in the mask won't hit anything");
  geo_query_validate_pos(ray->point);
  geo_query_validate_dir(ray->dir);

  GeoQueryRayHit bestHit  = {.time = f32_max};
  bool           foundHit = false;

  /**
   * Spheres.
   */
  const GeoQueryPrim* spheres      = &env->prims[GeoQueryPrim_Sphere];
  const GeoSphere*    spheresBegin = spheres->shapes;
  const GeoSphere*    spheresEnd   = spheresBegin + spheres->count;
  for (const GeoSphere* itr = spheresBegin; itr != spheresEnd; ++itr) {
    const f32 hitT = geo_sphere_intersect_ray(itr, ray);
    if (hitT < 0.0) {
      continue; // Miss.
    }
    const u64           shapeId    = spheres->ids[itr - spheresBegin];
    const GeoQueryLayer shapeLayer = spheres->layers[itr - spheresBegin];
    if (hitT < bestHit.time && geo_query_filter(filter, shapeId, shapeLayer)) {
      const GeoVector hitPos = geo_ray_position(ray, hitT);
      bestHit.time           = hitT;
      bestHit.shapeId        = shapeId;
      bestHit.normal         = geo_vector_norm(geo_vector_sub(hitPos, itr->point));
      foundHit               = true;
    }
  }

  /**
   * Capsules.
   */
  const GeoQueryPrim* capsules      = &env->prims[GeoQueryPrim_Capsule];
  const GeoCapsule*   capsulesBegin = capsules->shapes;
  const GeoCapsule*   capsulesEnd   = capsulesBegin + capsules->count;
  for (const GeoCapsule* itr = capsulesBegin; itr != capsulesEnd; ++itr) {
    GeoVector normal;
    const f32 hitT = geo_capsule_intersect_ray(itr, ray, &normal);
    if (hitT < 0.0) {
      continue; // Miss.
    }
    const u64           shapeId    = capsules->ids[itr - capsulesBegin];
    const GeoQueryLayer shapeLayer = capsules->layers[itr - capsulesBegin];
    if (hitT < bestHit.time && geo_query_filter(filter, shapeId, shapeLayer)) {
      bestHit.time    = hitT;
      bestHit.shapeId = shapeId;
      bestHit.normal  = normal;
      foundHit        = true;
    }
  }

  /**
   * Rotated boxes.
   */
  const GeoQueryPrim*  rotatedBoxes      = &env->prims[GeoQueryPrim_BoxRotated];
  const GeoBoxRotated* rotatedBoxesBegin = rotatedBoxes->shapes;
  const GeoBoxRotated* rotatedBoxesEnd   = rotatedBoxesBegin + rotatedBoxes->count;
  for (const GeoBoxRotated* itr = rotatedBoxesBegin; itr != rotatedBoxesEnd; ++itr) {
    GeoVector normal;
    const f32 hitT = geo_box_rotated_intersect_ray(itr, ray, &normal);
    if (hitT < 0.0) {
      continue; // Miss.
    }
    const u64           shapeId    = rotatedBoxes->ids[itr - rotatedBoxesBegin];
    const GeoQueryLayer shapeLayer = rotatedBoxes->layers[itr - rotatedBoxesBegin];
    if (hitT < bestHit.time && geo_query_filter(filter, shapeId, shapeLayer)) {
      bestHit.time    = hitT;
      bestHit.shapeId = shapeId;
      bestHit.normal  = normal;
      foundHit        = true;
    }
  }

  if (foundHit) {
    *outHit = bestHit;
  }
  return foundHit;
}

u32 geo_query_frustum_all(
    const GeoQueryEnv* env, const GeoVector frustum[8], u64 out[geo_query_max_hits]) {
  u32 count = 0;

  /**
   * Spheres.
   * TODO: Implement sphere <-> frustum intersection instead of converting spheres to boxes.
   */
  const GeoQueryPrim* spheres      = &env->prims[GeoQueryPrim_Sphere];
  const GeoSphere*    spheresBegin = spheres->shapes;
  const GeoSphere*    spheresEnd   = spheresBegin + spheres->count;
  for (const GeoSphere* itr = spheresBegin; itr != spheresEnd; ++itr) {
    const GeoBoxRotated box = {
        .box      = geo_box_from_sphere(itr->point, itr->radius),
        .rotation = geo_quat_ident,
    };
    if (LIKELY(count < geo_query_max_hits) && geo_box_rotated_intersect_frustum(&box, frustum)) {
      out[count++] = spheres->ids[itr - spheresBegin];
    }
  }

  /**
   * Capsules.
   * TODO: Implement capsule <-> frustum intersection instead of converting capsules to boxes.
   */
  const GeoQueryPrim* capsules      = &env->prims[GeoQueryPrim_Capsule];
  const GeoCapsule*   capsulesBegin = capsules->shapes;
  const GeoCapsule*   capsulesEnd   = capsulesBegin + capsules->count;
  for (const GeoCapsule* itr = capsulesBegin; itr != capsulesEnd; ++itr) {
    const GeoBoxRotated box = geo_box_rotated_from_capsule(itr->line.a, itr->line.b, itr->radius);
    if (LIKELY(count < geo_query_max_hits) && geo_box_rotated_intersect_frustum(&box, frustum)) {
      out[count++] = capsules->ids[itr - capsulesBegin];
    }
  }

  /**
   * Rotated boxes.
   */
  const GeoQueryPrim*  rotatedBoxes      = &env->prims[GeoQueryPrim_BoxRotated];
  const GeoBoxRotated* rotatedBoxesBegin = rotatedBoxes->shapes;
  const GeoBoxRotated* rotatedBoxesEnd   = rotatedBoxesBegin + rotatedBoxes->count;
  for (const GeoBoxRotated* itr = rotatedBoxesBegin; itr != rotatedBoxesEnd; ++itr) {
    if (LIKELY(count < geo_query_max_hits) && geo_box_rotated_intersect_frustum(itr, frustum)) {
      out[count++] = rotatedBoxes->ids[itr - rotatedBoxesBegin];
    }
  }

  return count;
}
