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

static f32 geo_prim_intersect_ray(
    const GeoQueryPrim* prim, const u32 entryIdx, const GeoRay* ray, GeoVector* outNormal) {
  switch (prim->type) {
  case GeoQueryPrim_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)prim->shapes)[entryIdx];
    const f32        hitT   = geo_sphere_intersect_ray(sphere, ray);
    if (hitT >= 0) {
      *outNormal = geo_vector_norm(geo_vector_sub(geo_ray_position(ray, hitT), sphere->point));
    }
  }
  case GeoQueryPrim_Capsule: {
    const GeoCapsule* capsule = &((const GeoCapsule*)prim->shapes)[entryIdx];
    return geo_capsule_intersect_ray(capsule, ray, outNormal);
  }
  case GeoQueryPrim_BoxRotated: {
    const GeoBoxRotated* boxRotated = &((const GeoBoxRotated*)prim->shapes)[entryIdx];
    return geo_box_rotated_intersect_ray(boxRotated, ray, outNormal);
  }
  case GeoQueryPrim_Count:
    break;
  }
  UNREACHABLE
}

static bool geo_prim_intersect_frustum(
    const GeoQueryPrim* prim, const u32 entryIdx, const GeoVector frustum[8]) {
  switch (prim->type) {
  case GeoQueryPrim_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)prim->shapes)[entryIdx];
    // * TODO: Implement sphere <-> frustum intersection instead of converting spheres to boxes.
    const GeoBoxRotated box = {
        .box      = geo_box_from_sphere(sphere->point, sphere->radius),
        .rotation = geo_quat_ident,
    };
    return geo_box_rotated_intersect_frustum(&box, frustum);
  }
  case GeoQueryPrim_Capsule: {
    const GeoCapsule* cap = &((const GeoCapsule*)prim->shapes)[entryIdx];
    // * TODO: Implement capsule <-> frustum intersection instead of converting capsules to boxes.
    const GeoBoxRotated box = geo_box_rotated_from_capsule(cap->line.a, cap->line.b, cap->radius);
    return geo_box_rotated_intersect_frustum(&box, frustum);
  }
  case GeoQueryPrim_BoxRotated: {
    const GeoBoxRotated* box = &((const GeoBoxRotated*)prim->shapes)[entryIdx];
    return geo_box_rotated_intersect_frustum(box, frustum);
  }
  case GeoQueryPrim_Count:
    break;
  }
  UNREACHABLE
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

static bool geo_query_filter_layer(const GeoQueryFilter* filter, const GeoQueryLayer shapeLayer) {
  return (filter->layerMask & shapeLayer) != 0;
}

static bool geo_query_filter_callback(const GeoQueryFilter* filter, const u64 shapeId) {
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

  for (u32 primIdx = 0; primIdx != GeoQueryPrim_Count; ++primIdx) {
    const GeoQueryPrim* prim = &env->prims[primIdx];
    for (u32 i = 0; i != prim->count; ++i) {
      if (!geo_query_filter_layer(filter, prim->layers[i])) {
        continue; // Layer not included in filter.
      }
      GeoVector normal;
      const f32 hitT = geo_prim_intersect_ray(prim, i, ray, &normal);
      if (hitT < 0.0) {
        continue; // Miss.
      }
      if (hitT >= bestHit.time) {
        continue; // Better hit already found.
      }
      if (!geo_query_filter_callback(filter, prim->ids[i])) {
        continue; // Filtered out by the filter's callback.
      }

      // New best hit.
      bestHit.time    = hitT;
      bestHit.shapeId = prim->ids[i];
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

  for (u32 primIdx = 0; primIdx != GeoQueryPrim_Count; ++primIdx) {
    const GeoQueryPrim* prim = &env->prims[primIdx];
    for (u32 i = 0; i != prim->count; ++i) {
      if (geo_prim_intersect_frustum(prim, i, frustum)) {
        out[count++] = prim->ids[i];
        if (UNLIKELY(count == geo_query_max_hits)) {
          goto MaxCountReached;
        }
      }
    }
  }

MaxCountReached:
  return count;
}
