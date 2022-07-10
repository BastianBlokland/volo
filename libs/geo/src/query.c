#include "core_alloc.h"
#include "core_dynarray.h"
#include "geo_query.h"

struct sGeoQueryEnv {
  Allocator* alloc;
  DynArray   spheres;    // GeoSphere[].
  DynArray   sphereIds;  // u64[].
  DynArray   capsules;   // GeoCapsule[].
  DynArray   capsuleIds; // u64[].
};

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
  *dynarray_push_t(&env->spheres, GeoSphere) = sphere;
  *dynarray_push_t(&env->sphereIds, u64)     = id;
}

void geo_query_insert_capsule(GeoQueryEnv* env, const GeoCapsule capsule, const u64 id) {
  *dynarray_push_t(&env->capsules, GeoCapsule) = capsule;
  *dynarray_push_t(&env->capsuleIds, u64)      = id;
}

bool geo_query_ray(GeoQueryEnv* env, const GeoRay* ray, GeoQueryRayHit* outHit) {
  GeoQueryRayHit bestHit  = {.time = f32_max};
  bool           foundHit = false;

  GeoSphere* spheresBegin = dynarray_begin_t(&env->spheres, GeoSphere);
  GeoSphere* spheresEnd   = dynarray_end_t(&env->spheres, GeoSphere);
  for (GeoSphere* itr = spheresBegin; itr != spheresEnd; ++itr) {
    GeoVector normal;
    const f32 hitT = geo_sphere_intersect_ray(itr, ray, &normal);
    if (hitT < 0.0) {
      continue; // Miss.
    }
    if (hitT < bestHit.time) {
      bestHit.time    = hitT;
      bestHit.shapeId = *dynarray_at_t(&env->sphereIds, itr - spheresBegin, u64);
      bestHit.normal  = normal;
      foundHit        = true;
    }
  }

  if (foundHit) {
    *outHit = bestHit;
  }
  return foundHit;
}
