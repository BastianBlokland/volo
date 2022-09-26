#pragma once
#include "geo_box_rotated.h"
#include "geo_capsule.h"
#include "geo_ray.h"
#include "geo_sphere.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Maximum number of objects that can be hit using a single query, additional objects are ignored.
 */
#define geo_query_max_hits 128

/**
 * Callback for filtering query hits.
 * Return 'true' to keep the hit or 'false' to discard the hit.
 */
typedef struct {
  const void* context;                                // Optional.
  bool (*callback)(const void* context, u64 shapeId); // Optional.
} GeoQueryFilter;

/**
 * Environment for querying against.
 */
typedef struct sGeoQueryEnv GeoQueryEnv;

/**
 * Create a new GeoQueryEnv instance.
 * Destroy using 'geo_query_env_destroy()'.
 */
GeoQueryEnv* geo_query_env_create(Allocator*);

/**
 * Destroy a GeoQueryEnv instance.
 */
void geo_query_env_destroy(GeoQueryEnv*);

/**
 * Clear all shapes from the environment.
 */
void geo_query_env_clear(GeoQueryEnv*);

/**
 * Insert geometric shapes into the environment.
 */
void geo_query_insert_sphere(GeoQueryEnv*, GeoSphere, u64 id);
void geo_query_insert_capsule(GeoQueryEnv*, GeoCapsule, u64 id);
void geo_query_insert_box_rotated(GeoQueryEnv*, GeoBoxRotated, u64 id);

typedef struct {
  f32       time;
  u64       shapeId;
  GeoVector normal;
} GeoQueryRayHit;

/**
 * Query for a hit along the given ray.
 * Returns true if a shape was hit otherwise false.
 * NOTE: Hit information is written to the out pointer if true was returned.
 */
bool geo_query_ray(
    const GeoQueryEnv*, const GeoRay*, const GeoQueryFilter*, GeoQueryRayHit* outHit);

/**
 * Query for all objects that are contained in the frustum formed by the given 8 corner points.
 * NOTE: Returns the number of hit objects.
 */
u32 geo_query_frustum_all(
    const GeoQueryEnv*, const GeoVector frustum[8], u64 out[geo_query_max_hits]);
