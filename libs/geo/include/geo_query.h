#pragma once
#include "geo.h"
#include "geo_vector.h"

/**
 * Maximum number of objects that can be hit using a single query, additional objects are ignored.
 */
#define geo_query_max_hits 512

/**
 * Geometry layer mask.
 */
typedef u32 GeoQueryLayer;

/**
 * Callback for filtering potential query hits.
 * Return 'true' to check the shape or 'false' to discard the shape.
 */
typedef struct {
  const void* context;                                                    // Optional.
  bool (*callback)(const void* context, u64 shapeUserId, u32 shapeLayer); // Optional.
  GeoQueryLayer layerMask;
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
 * NOTE: Invalidates previous build.
 */
void geo_query_env_clear(GeoQueryEnv*);

/**
 * Insert geometric shapes into the environment.
 * NOTE: Call 'geo_query_build()' after inserting all the shapes.
 */
void geo_query_insert_sphere(GeoQueryEnv*, GeoSphere, u64 userId, GeoQueryLayer);
void geo_query_insert_capsule(GeoQueryEnv*, GeoCapsule, u64 userId, GeoQueryLayer);
void geo_query_insert_box_rotated(GeoQueryEnv*, GeoBoxRotated, u64 userId, GeoQueryLayer);

/**
 * Build the query using all the inserted shapes.
 */
void geo_query_build(GeoQueryEnv*);

typedef struct {
  f32           time;
  u64           userId;
  GeoVector     normal;
  GeoQueryLayer layer;
} GeoQueryRayHit;

/**
 * Query for a hit along the given ray.
 * Returns true if a shape was hit otherwise false.
 * NOTE: Hit information is written to the out pointer if true was returned.
 */
bool geo_query_ray(
    const GeoQueryEnv*, const GeoRay*, f32 maxDist, const GeoQueryFilter*, GeoQueryRayHit* outHit);

/**
 * Query for a hit along the given ray with radius.
 * Returns true if a shape was hit otherwise false.
 * NOTE: Hit information is written to the out pointer if true was returned.
 */
bool geo_query_ray_fat(
    const GeoQueryEnv*,
    const GeoRay*,
    f32 radius,
    f32 maxDist,
    const GeoQueryFilter*,
    GeoQueryRayHit* outHit);

/**
 * Query for all objects that are contained in the given sphere.
 * NOTE: Returns the number of hit objects.
 */
u32 geo_query_sphere_all(
    const GeoQueryEnv*,
    const GeoSphere*,
    const GeoQueryFilter*,
    u64 out[PARAM_ARRAY_SIZE(geo_query_max_hits)]);

/**
 * Query for all objects that are contained in the given box.
 * NOTE: Returns the number of hit objects.
 */
u32 geo_query_box_all(
    const GeoQueryEnv*,
    const GeoBoxRotated*,
    const GeoQueryFilter*,
    u64 out[PARAM_ARRAY_SIZE(geo_query_max_hits)]);

/**
 * Query for all objects that are contained in the frustum formed by the given 8 corner points.
 * NOTE: Returns the number of hit objects.
 */
u32 geo_query_frustum_all(
    const GeoQueryEnv*,
    const GeoVector frustum[PARAM_ARRAY_SIZE(8)],
    const GeoQueryFilter*,
    u64 out[PARAM_ARRAY_SIZE(geo_query_max_hits)]);

/**
 * Query statistics.
 */

u32           geo_query_node_count(const GeoQueryEnv*);
const GeoBox* geo_query_node_bounds(const GeoQueryEnv*, u32 index);
u32           geo_query_node_depth(const GeoQueryEnv*, u32 index);

typedef enum {
  GeoQueryStat_PrimSphereCount,
  GeoQueryStat_PrimCapsuleCount,
  GeoQueryStat_PrimBoxRotatedCount,
  GeoQueryStat_QueryRayCount,
  GeoQueryStat_QueryRayFatCount,
  GeoQueryStat_QuerySphereAllCount,
  GeoQueryStat_QueryBoxAllCount,
  GeoQueryStat_QueryFrustumAllCount,
  GeoQueryStat_BvhNodes,
  GeoQueryStat_BvhMaxDepth,

  GeoQueryStat_Count,
} GeoQueryStat;

void geo_query_stats_reset(GeoQueryEnv*);
i32* geo_query_stats(GeoQueryEnv*);
