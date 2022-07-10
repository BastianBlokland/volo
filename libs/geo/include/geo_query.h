#pragma once
#include "geo_capsule.h"
#include "geo_ray.h"
#include "geo_sphere.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

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
bool geo_query_ray(GeoQueryEnv*, const GeoRay*, GeoQueryRayHit* outHit);
