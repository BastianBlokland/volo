#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "geo_query.h"

struct sGeoQueryEnv {
  Allocator* alloc;
  DynArray   spheres;    // GeoSphere[].
  DynArray   sphereIds;  // u64[].
  DynArray   capsules;   // GeoCapsule[].
  DynArray   capsuleIds; // u64[].
};

static void geo_query_validate_pos(const GeoVector vec) {
  // Constrain the positions 1000 meters from the origin to avoid precision issues.
  diag_assert_msg(
      geo_vector_mag_sqr(vec) <= (1e4f * 1e4f),
      "Position ({}) is out of bounds",
      geo_vector_fmt(vec));
}

static void geo_query_validate_dir(const GeoVector vec) {
  diag_assert_msg(
      math_abs(geo_vector_mag_sqr(vec) - 1.0f) <= 1e-6f,
      "Direction ({}) is not normalized",
      geo_vector_fmt(vec));
  return;
}

GeoQueryEnv* geo_query_env_create(Allocator* alloc) {
  GeoQueryEnv* env = alloc_alloc_t(alloc, GeoQueryEnv);

  *env = (GeoQueryEnv){
      .alloc      = alloc,
      .spheres    = dynarray_create_t(alloc, GeoSphere, 512),
      .sphereIds  = dynarray_create_t(alloc, u64, 512),
      .capsules   = dynarray_create_t(alloc, GeoCapsule, 512),
      .capsuleIds = dynarray_create_t(alloc, u64, 512),
  };
  return env;
}

void geo_query_env_destroy(GeoQueryEnv* env) {
  dynarray_destroy(&env->spheres);
  dynarray_destroy(&env->sphereIds);
  dynarray_destroy(&env->capsules);
  dynarray_destroy(&env->capsuleIds);
  alloc_free_t(env->alloc, env);
}

void geo_query_env_clear(GeoQueryEnv* env) {
  dynarray_clear(&env->spheres);
  dynarray_clear(&env->sphereIds);
  dynarray_clear(&env->capsules);
  dynarray_clear(&env->capsuleIds);
}

void geo_query_insert_sphere(GeoQueryEnv* env, const GeoSphere sphere, const u64 id) {
  geo_query_validate_pos(sphere.point);

  *dynarray_push_t(&env->spheres, GeoSphere) = sphere;
  *dynarray_push_t(&env->sphereIds, u64)     = id;
}

void geo_query_insert_capsule(GeoQueryEnv* env, const GeoCapsule capsule, const u64 id) {
  geo_query_validate_pos(capsule.line.a);
  geo_query_validate_pos(capsule.line.b);

  *dynarray_push_t(&env->capsules, GeoCapsule) = capsule;
  *dynarray_push_t(&env->capsuleIds, u64)      = id;
}

bool geo_query_ray(const GeoQueryEnv* env, const GeoRay* ray, GeoQueryRayHit* outHit) {
  geo_query_validate_pos(ray->point);
  geo_query_validate_dir(ray->dir);

  GeoQueryRayHit bestHit  = {.time = f32_max};
  bool           foundHit = false;

  /**
   * Spheres.
   */
  const GeoSphere* spheresBegin = dynarray_begin_t(&env->spheres, GeoSphere);
  const GeoSphere* spheresEnd   = dynarray_end_t(&env->spheres, GeoSphere);
  for (const GeoSphere* itr = spheresBegin; itr != spheresEnd; ++itr) {
    const f32 hitT = geo_sphere_intersect_ray(itr, ray);
    if (hitT < 0.0) {
      continue; // Miss.
    }
    if (hitT < bestHit.time) {
      const GeoVector hitPos = geo_ray_position(ray, hitT);
      bestHit.time           = hitT;
      bestHit.shapeId        = *dynarray_at_t(&env->sphereIds, itr - spheresBegin, u64);
      bestHit.normal         = geo_vector_norm(geo_vector_sub(hitPos, itr->point));
      foundHit               = true;
    }
  }

  /**
   * Capsules.
   */
  const GeoCapsule* capsulesBegin = dynarray_begin_t(&env->capsules, GeoCapsule);
  const GeoCapsule* capsulesEnd   = dynarray_end_t(&env->capsules, GeoCapsule);
  for (const GeoCapsule* itr = capsulesBegin; itr != capsulesEnd; ++itr) {
    GeoVector normal;
    const f32 hitT = geo_capsule_intersect_ray(itr, ray, &normal);
    if (hitT < 0.0) {
      continue; // Miss.
    }
    if (hitT < bestHit.time) {
      bestHit.time    = hitT;
      bestHit.shapeId = *dynarray_at_t(&env->capsuleIds, itr - capsulesBegin, u64);
      bestHit.normal  = normal;
      foundHit        = true;
    }
  }

  if (foundHit) {
    *outHit = bestHit;
  }
  return foundHit;
}
