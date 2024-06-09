#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "core_thread.h"
#include "geo_box_rotated.h"
#include "geo_capsule.h"
#include "geo_query.h"
#include "geo_sphere.h"

#define geo_query_shape_align 16
#define geo_query_bvh_node_divide_threshold 8

ASSERT(alignof(GeoSphere) <= geo_query_shape_align, "Insufficient alignment")
ASSERT(alignof(GeoCapsule) <= geo_query_shape_align, "Insufficient alignment")
ASSERT(alignof(GeoBoxRotated) <= geo_query_shape_align, "Insufficient alignment")

typedef u32 QueryShape;

typedef enum {
  QueryPrimType_Sphere,
  QueryPrimType_Capsule,
  QueryPrimType_BoxRotated,

  QueryPrimType_Count,
} QueryPrimType;

typedef struct {
  u32            count, capacity;
  u64*           userIds;
  GeoQueryLayer* layers;
  GeoBox*        bounds;
  void*          data; // GeoSphere[] / GeoCapsule[] / GeoBoxRotated[]
} QueryPrim;

typedef QueryPrim QueryPrimStorage[QueryPrimType_Count];

/**
 * There are two types of BVH nodes:
 * - Leaf node: Contains 'shapeCount' shapes starting from 'childIndex' in the shapes array.
 * - Parent node: Contains two child nodes starting at 'childIndex' in the nodes array.
 * The node-type can be determined by the 'shapeCount': '> 0' for leaf-node, '== 0' for parent node.
 */
typedef struct {
  GeoBox        bounds;
  GeoQueryLayer layers;
  u32           depth; // Only for debug purposes, could be removed if needed.
  u32           childIndex, shapeCount;
} QueryBvhNode;

typedef struct {
  QueryBvhNode* nodes;  // QueryBvhNode[capacity * 2]
  QueryShape*   shapes; // QueryShape[capacity]
  u32           nodeCount, shapeCapacity;
} QueryBvh;

struct sGeoQueryEnv {
  Allocator*       alloc;
  QueryBvh         bvh;
  QueryPrimStorage prims;
  i32              stats[GeoQueryStat_Count];
};

static bool query_filter_layer(const GeoQueryFilter* f, const GeoQueryLayer shapeLayer) {
  return (f->layerMask & shapeLayer) != 0;
}

static bool query_filter_callback(const GeoQueryFilter* f, const u64 userId, const u32 layer) {
  if (f->callback) {
    return f->callback(f->context, userId, layer);
  }
  return true;
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

static usize prim_data_size(const QueryPrimType type) {
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
  const usize dataSize = prim_data_size(type) * capacity;
  return (QueryPrim){
      .capacity = capacity,
      .userIds  = alloc_array_t(g_allocHeap, u64, capacity),
      .layers   = alloc_array_t(g_allocHeap, GeoQueryLayer, capacity),
      .bounds   = alloc_array_t(g_allocHeap, GeoBox, capacity),
      .data     = alloc_alloc(g_allocHeap, dataSize, geo_query_shape_align).ptr,
  };
}

static void prim_destroy(QueryPrim* p, const QueryPrimType type) {
  alloc_free_array_t(g_allocHeap, p->userIds, p->capacity);
  alloc_free_array_t(g_allocHeap, p->layers, p->capacity);
  alloc_free_array_t(g_allocHeap, p->bounds, p->capacity);
  alloc_free(g_allocHeap, mem_create(p->data, prim_data_size(type) * p->capacity));
}

static void prim_copy(QueryPrim* dst, const QueryPrim* src, const QueryPrimType type) {
  diag_assert(dst->capacity >= src->count);

#define cpy_entry_field(_FIELD_, _SIZE_)                                                           \
  mem_cpy(                                                                                         \
      mem_create(dst->_FIELD_, (_SIZE_)*dst->capacity),                                            \
      mem_create(src->_FIELD_, (_SIZE_)*src->count))

  cpy_entry_field(userIds, sizeof(u64));
  cpy_entry_field(layers, sizeof(GeoQueryLayer));
  cpy_entry_field(bounds, sizeof(GeoBox));
  cpy_entry_field(data, prim_data_size(type));

  dst->count = src->count;

#undef cpy_entry_field
}

NO_INLINE_HINT static void prim_grow(QueryPrim* p, const QueryPrimType type) {
  QueryPrim newPrim = prim_create(type, bits_nextpow2(p->capacity + 1));
  prim_copy(&newPrim, p, type);
  prim_destroy(p, type);
  *p = newPrim;
}

INLINE_HINT static void prim_ensure_next(QueryPrim* p, const QueryPrimType type) {
  if (LIKELY(p->capacity != p->count)) {
    return; // Enough space remaining.
  }
  prim_grow(p, type);
}

static QueryShape    shape_handle(const QueryPrimType type, const u32 i) { return type | (i << 8); }
static QueryPrimType shape_type(const QueryShape shape) { return (QueryPrimType)(shape & 0xff); }
static u32           shape_index(const QueryShape shape) { return shape >> 8; }

static const QueryPrim* shape_prim(const QueryShape shape, const QueryPrimStorage prims) {
  return &prims[shape_type(shape)];
}

static const GeoBox* shape_bounds(const QueryShape shape, const QueryPrimStorage prims) {
  return &shape_prim(shape, prims)->bounds[shape_index(shape)];
}

static GeoQueryLayer shape_layer(const QueryShape shape, const QueryPrimStorage prims) {
  return shape_prim(shape, prims)->layers[shape_index(shape)];
}

static u64 shape_user_id(const QueryShape shape, const QueryPrimStorage prims) {
  return shape_prim(shape, prims)->userIds[shape_index(shape)];
}

static u32 shape_count(const QueryPrimStorage prims) {
  u32 result = 0;
  for (QueryPrimType primType = 0; primType != QueryPrimType_Count; ++primType) {
    result += prims[primType].count;
  }
  return result;
}

static f32 shape_intersect_ray(
    const QueryShape shape, const QueryPrimStorage prims, const GeoRay* ray, GeoVector* outNormal) {
  const QueryPrimType primType = shape_type(shape);
  const QueryPrim*    prim     = &prims[primType];
  switch (primType) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)prim->data)[shape_index(shape)];
    return geo_sphere_intersect_ray_info(sphere, ray, outNormal);
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* capsule = &((const GeoCapsule*)prim->data)[shape_index(shape)];
    return geo_capsule_intersect_ray_info(capsule, ray, outNormal);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* boxRotated = &((const GeoBoxRotated*)prim->data)[shape_index(shape)];
    return geo_box_rotated_intersect_ray_info(boxRotated, ray, outNormal);
  }
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static f32 shape_intersect_ray_fat(
    const QueryShape       shape,
    const QueryPrimStorage prims,
    const GeoRay*          ray,
    const f32              radius,
    GeoVector*             outNormal) {
  const QueryPrimType primType = shape_type(shape);
  const QueryPrim*    prim     = &prims[primType];
  switch (primType) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere        = &((const GeoSphere*)prim->data)[shape_index(shape)];
    const GeoSphere  sphereDilated = geo_sphere_dilate(sphere, radius);
    return geo_sphere_intersect_ray_info(&sphereDilated, ray, outNormal);
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* capsule        = &((const GeoCapsule*)prim->data)[shape_index(shape)];
    const GeoCapsule  capsuleDilated = geo_capsule_dilate(capsule, radius);
    return geo_capsule_intersect_ray_info(&capsuleDilated, ray, outNormal);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* boxRotated = &((const GeoBoxRotated*)prim->data)[shape_index(shape)];
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

static bool
shape_overlap_sphere(const QueryShape shape, const QueryPrimStorage prims, const GeoSphere* tgt) {
  const QueryPrimType primType = shape_type(shape);
  const QueryPrim*    prim     = &prims[primType];
  switch (primType) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)prim->data)[shape_index(shape)];
    return geo_sphere_overlap(sphere, tgt);
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* cap = &((const GeoCapsule*)prim->data)[shape_index(shape)];
    return geo_capsule_overlap_sphere(cap, tgt);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* box = &((const GeoBoxRotated*)prim->data)[shape_index(shape)];
    return geo_box_rotated_overlap_sphere(box, tgt);
  }
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static bool shape_overlap_box_rotated(
    const QueryShape shape, const QueryPrimStorage prims, const GeoBoxRotated* tgt) {
  const QueryPrimType primType = shape_type(shape);
  const QueryPrim*    prim     = &prims[primType];
  switch (primType) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)prim->data)[shape_index(shape)];
    return geo_box_rotated_overlap_sphere(tgt, sphere);
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* cap = &((const GeoCapsule*)prim->data)[shape_index(shape)];
    // TODO: Implement capsule <-> rotated-box overlap instead of converting capsules to boxes.
    const GeoBoxRotated box = geo_box_rotated_from_capsule(cap->line.a, cap->line.b, cap->radius);
    return geo_box_rotated_overlap_box_rotated(&box, tgt);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* box = &((const GeoBoxRotated*)prim->data)[shape_index(shape)];
    return geo_box_rotated_overlap_box_rotated(box, tgt);
  }
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static bool shape_overlap_frustum(
    const QueryShape       shape,
    const QueryPrimStorage prims,
    const GeoVector        frustum[PARAM_ARRAY_SIZE(8)]) {
  const QueryPrimType primType = shape_type(shape);
  const QueryPrim*    prim     = &prims[primType];
  switch (primType) {
  case QueryPrimType_Sphere: {
    const GeoSphere* sphere = &((const GeoSphere*)prim->data)[shape_index(shape)];
    return geo_sphere_overlap_frustum(sphere, frustum);
  }
  case QueryPrimType_Capsule: {
    const GeoCapsule* cap = &((const GeoCapsule*)prim->data)[shape_index(shape)];
    // TODO: Implement capsule <-> frustum overlap instead of converting capsules to boxes.
    const GeoBoxRotated box = geo_box_rotated_from_capsule(cap->line.a, cap->line.b, cap->radius);
    return geo_box_rotated_overlap_frustum(&box, frustum);
  }
  case QueryPrimType_BoxRotated: {
    const GeoBoxRotated* box = &((const GeoBoxRotated*)prim->data)[shape_index(shape)];
    return geo_box_rotated_overlap_frustum(box, frustum);
  }
  case QueryPrimType_Count:
    break;
  }
  UNREACHABLE
}

static QueryShape bvh_shape(const QueryBvh* bvh, const u32 shapeIdx) {
  return bvh->shapes[shapeIdx];
}

/**
 * Return the amount of shapes in a node. If non-zero its a leaf-node otherwise its a parent node.
 */
static u32 bvh_shape_count(const QueryBvh* bvh, const u32 nodeIdx) {
  return bvh->nodes[nodeIdx].shapeCount;
}

static void bvh_clear(QueryBvh* bvh) { bvh->nodeCount = 0; }

static void bvh_grow_if_needed(QueryBvh* bvh, const u32 shapeCount) {
  if (bvh->shapeCapacity >= shapeCount) {
    return; // Already enough capacity.
  }
  if (bvh->shapeCapacity) {
    alloc_free_array_t(g_allocHeap, bvh->nodes, bvh->shapeCapacity * 2);
    alloc_free_array_t(g_allocHeap, bvh->shapes, bvh->shapeCapacity);
  }
  bvh->shapeCapacity = bits_nextpow2(shapeCount);
  bvh->nodes         = alloc_array_t(g_allocHeap, QueryBvhNode, bvh->shapeCapacity * 2);
  bvh->shapes        = alloc_array_t(g_allocHeap, QueryShape, bvh->shapeCapacity);
}

static void bvh_swap_shape(QueryBvh* bvh, const u32 a, const u32 b) {
  const QueryShape tmp = bvh->shapes[a];
  bvh->shapes[a]       = bvh->shapes[b];
  bvh->shapes[b]       = tmp;
}

/**
 * Insert a single root leaf-node containing all the shapes (needs at least 1 shape).
 * NOTE: Bvh needs to be empty before inserting a new root.
 * Returns the node index.
 */
static u32 bvh_insert_root(QueryBvh* bvh, const QueryPrimStorage prims) {
  diag_assert(!bvh->nodeCount);    // Bvh needs to be cleared before inserting a new root.
  diag_assert(shape_count(prims)); // Root node needs at least 1 shape.
  bvh_grow_if_needed(bvh, shape_count(prims));

  const u32     rootIndex = bvh->nodeCount++; // Always index 0 at the moment.
  QueryBvhNode* root      = &bvh->nodes[rootIndex];
  *root                   = (QueryBvhNode){.bounds = geo_box_inverted3()};

  for (QueryPrimType primType = 0; primType != QueryPrimType_Count; ++primType) {
    const QueryPrim* prim = &prims[primType];
    for (u32 primIdx = 0; primIdx != prim->count; ++primIdx) {
      root->layers |= prim->layers[primIdx];
      root->bounds = geo_box_encapsulate_box(&root->bounds, &prim->bounds[primIdx]);
      bvh->shapes[root->shapeCount++] = shape_handle(primType, primIdx);
    }
  }
  return rootIndex;
}

/**
 * Insert a new child leaf-node with the specified shapes.
 * NOTE: Shapes need to be consecutively stored.
 * Returns the node index.
 */
static u32 bvh_insert(
    QueryBvh*              bvh,
    const QueryPrimStorage prims,
    const u32              depth,
    const u32              shapeBegin,
    const u32              shapeCount) {
  const u32     index = bvh->nodeCount++;
  QueryBvhNode* node  = &bvh->nodes[index];

  *node = (QueryBvhNode){
      .bounds     = geo_box_inverted3(),
      .depth      = depth,
      .childIndex = shapeBegin,
      .shapeCount = shapeCount,
  };

  for (u32 i = 0; i != shapeCount; ++i) {
    const QueryShape shape = bvh_shape(bvh, shapeBegin + i);
    node->layers |= shape_layer(shape, prims);
    node->bounds = geo_box_encapsulate_box(&node->bounds, shape_bounds(shape, prims));
  }
  return index;
}

typedef struct {
  u32 axis;
  f32 pos;
} QueryBvhPlane;

/**
 * Pick a plane to split the leaf-node on.
 * At the moment we just use the center of the longest axis of the node.
 */
static QueryBvhPlane bvh_split_pick(const QueryBvh* bvh, const u32 nodeIdx) {
  const QueryBvhNode* node = &bvh->nodes[nodeIdx];
  diag_assert(node->shapeCount); // Only leaf-nodes can be split.
  const GeoVector nodeSize = geo_box_size(&node->bounds);
  u32             axis     = 0;
  if (nodeSize.y > nodeSize.x) {
    axis = 1;
  }
  if (nodeSize.z > nodeSize.comps[axis]) {
    axis = 2;
  }
  const f32 min  = node->bounds.min.comps[axis];
  const f32 size = nodeSize.comps[axis];
  return (QueryBvhPlane){.axis = axis, .pos = min + size * 0.5f};
}

/**
 * Partition the leaf-node so all shapes before the returned shape index are on one side of the
 * plane and all shapes after on the other side.
 */
static u32 bvh_partition(
    QueryBvh* bvh, const QueryPrimStorage prims, const u32 nodeIdx, const QueryBvhPlane* plane) {
  QueryBvhNode* node = &bvh->nodes[nodeIdx];
  diag_assert(node->shapeCount); // Only leaf-nodes can be partitioned.
  u32 shapeLeft  = node->childIndex;
  u32 shapeRight = shapeLeft + node->shapeCount - 1;
  for (;;) {
    const GeoBox* leftBounds = shape_bounds(bvh_shape(bvh, shapeLeft), prims);
    const f32     leftMin    = leftBounds->min.comps[plane->axis];
    const f32     leftMax    = leftBounds->max.comps[plane->axis];
    const f32     leftCenter = (leftMin + leftMax) * 0.5f;
    if (leftCenter < plane->pos) {
      ++shapeLeft;
      if (shapeLeft > shapeRight) {
        break;
      }
    } else {
      if (shapeLeft == shapeRight) {
        break;
      }
      bvh_swap_shape(bvh, shapeLeft, shapeRight);
      --shapeRight;
    }
  }
  return shapeLeft;
}

/**
 * Subdivide the given leaf-node, if successful the node is no longer a leaf-node but contains a
 * tree of child nodes encompassing the same shapes as it did before subdividing.
 */
static void bvh_subdivide(QueryBvh* bvh, const QueryPrimStorage prims, const u32 nodeIdx) {
  QueryBvhNode* node = &bvh->nodes[nodeIdx];
  diag_assert(node->shapeCount); // Only leaf-nodes can be subdivided.

  const QueryBvhPlane partitionPlane = bvh_split_pick(bvh, nodeIdx);
  const u32           partitionIndex = bvh_partition(bvh, prims, nodeIdx, &partitionPlane);

  const u32 countA = partitionIndex - node->childIndex;
  const u32 countB = node->shapeCount - countA;
  if (!countA || !countB) {
    return; // One of the partitions is empty; abort the subdivide.
  }

  const u32 childA = bvh_insert(bvh, prims, node->depth + 1, node->childIndex, countA);
  const u32 childB = bvh_insert(bvh, prims, node->depth + 1, partitionIndex, countB);

  node->childIndex = childA;
  node->shapeCount = 0;              // Node is no longer a leaf-node.
  diag_assert(childB == childA + 1); // Child nodes have to be stored consecutively.

  if (countA >= geo_query_bvh_node_divide_threshold) {
    bvh_subdivide(bvh, prims, childA);
  }
  if (countB >= geo_query_bvh_node_divide_threshold) {
    bvh_subdivide(bvh, prims, childB);
  }
}

static bool bvh_test_ray(
    const QueryBvh*       bvh,
    const u32             nodeIdx,
    const GeoQueryFilter* filter,
    const GeoRay*         ray,
    const f32             maxDist) {
  const QueryBvhNode* node = &bvh->nodes[nodeIdx];
  if (!query_filter_layer(filter, node->layers)) {
    return false; // Node does not contain any shapes that are included in the filter.
  }
  if (geo_box_contains3(&node->bounds, ray->point)) {
    return true; // Ray starts inside the node.
  }
  const f32 hitT = geo_box_intersect_ray(&node->bounds, ray);
  return hitT >= 0.0f && hitT < maxDist;
}

static bool bvh_test_ray_fat(
    const QueryBvh*       bvh,
    const u32             nodeIdx,
    const GeoQueryFilter* filter,
    const GeoRay*         ray,
    const f32             radius,
    const f32             maxDist) {
  const QueryBvhNode* node = &bvh->nodes[nodeIdx];
  if (!query_filter_layer(filter, node->layers)) {
    return false; // Node does not contain any shapes that are included in the filter.
  }
  const GeoVector dilateSize = geo_vector(radius, radius, radius);
  /**
   * Crude (conservative) estimation of a Minkowski-sum.
   * NOTE: Ignores the fact that the summed shape should have rounded corners, meaning we detect
   * intersections too early at the corners.
   */
  const GeoBox boundsDilated = geo_box_dilate(&node->bounds, dilateSize);
  if (geo_box_contains3(&boundsDilated, ray->point)) {
    return true; // Ray starts inside the node.
  }
  const f32 hitT = geo_box_intersect_ray(&boundsDilated, ray);
  return hitT >= 0.0f && hitT < maxDist;
}

static bool bvh_test_sphere(
    const QueryBvh* bvh, const u32 nodeIdx, const GeoQueryFilter* filter, const GeoSphere* sphere) {
  const QueryBvhNode* node = &bvh->nodes[nodeIdx];
  if (!query_filter_layer(filter, node->layers)) {
    return false; // Node does not contain any shapes that are included in the filter.
  }
  return geo_box_overlap_sphere(&node->bounds, sphere);
}

static bool bvh_test_box(
    const QueryBvh* bvh, const u32 nodeIdx, const GeoQueryFilter* filter, const GeoBox* box) {
  const QueryBvhNode* node = &bvh->nodes[nodeIdx];
  if (!query_filter_layer(filter, node->layers)) {
    return false; // Node does not contain any shapes that are included in the filter.
  }
  return geo_box_overlap(box, &node->bounds);
}

static bool bvh_test_box_rotated(
    const QueryBvh*       bvh,
    const u32             nodeIdx,
    const GeoQueryFilter* filter,
    const GeoBoxRotated*  boxRotated) {
  const QueryBvhNode* node = &bvh->nodes[nodeIdx];
  if (!query_filter_layer(filter, node->layers)) {
    return false; // Node does not contain any shapes that are included in the filter.
  }
  return geo_box_rotated_overlap_box(boxRotated, &node->bounds);
}

static void bvh_destroy(QueryBvh* bvh) {
  if (bvh->shapeCapacity) {
    alloc_free_array_t(g_allocHeap, bvh->nodes, bvh->shapeCapacity * 2);
    alloc_free_array_t(g_allocHeap, bvh->shapes, bvh->shapeCapacity);
  }
}

static void query_stat_add(const GeoQueryEnv* env, const GeoQueryStat stat, const i32 value) {
  GeoQueryEnv* mutableEnv = (GeoQueryEnv*)env;
  thread_atomic_add_i32(&mutableEnv->stats[stat], value);
}

GeoQueryEnv* geo_query_env_create(Allocator* alloc) {
  GeoQueryEnv* env = alloc_alloc_t(alloc, GeoQueryEnv);

  *env = (GeoQueryEnv){
      .alloc                           = alloc,
      .prims[QueryPrimType_Sphere]     = prim_create(QueryPrimType_Sphere, 32),
      .prims[QueryPrimType_Capsule]    = prim_create(QueryPrimType_Capsule, 32),
      .prims[QueryPrimType_BoxRotated] = prim_create(QueryPrimType_BoxRotated, 32),
  };

  return env;
}

void geo_query_env_destroy(GeoQueryEnv* env) {
  bvh_destroy(&env->bvh);
  for (QueryPrimType primType = 0; primType != QueryPrimType_Count; ++primType) {
    prim_destroy(&env->prims[primType], primType);
  }
  alloc_free_t(env->alloc, env);
}

void geo_query_env_clear(GeoQueryEnv* env) {
  bvh_clear(&env->bvh);
  array_for_t(env->prims, QueryPrim, prim) { prim->count = 0; }
}

void geo_query_insert_sphere(
    GeoQueryEnv* env, const GeoSphere sphere, const u64 userId, const GeoQueryLayer layer) {
  query_validate_pos(sphere.point);
  diag_assert_msg(layer, "Shape needs at least one layer");

  QueryPrim* prim = &env->prims[QueryPrimType_Sphere];
  prim_ensure_next(prim, QueryPrimType_Sphere);
  prim->userIds[prim->count]            = userId;
  prim->layers[prim->count]             = layer;
  prim->bounds[prim->count]             = geo_box_from_sphere(sphere.point, sphere.radius);
  ((GeoSphere*)prim->data)[prim->count] = sphere;
  ++prim->count;
}

void geo_query_insert_capsule(
    GeoQueryEnv* env, const GeoCapsule capsule, const u64 userId, const GeoQueryLayer layer) {
  query_validate_pos(capsule.line.a);
  query_validate_pos(capsule.line.b);
  diag_assert_msg(layer, "Shape needs at least one layer");

  QueryPrim* prim = &env->prims[QueryPrimType_Capsule];
  prim_ensure_next(prim, QueryPrimType_Capsule);
  prim->userIds[prim->count] = userId;
  prim->layers[prim->count]  = layer;
  prim->bounds[prim->count]  = geo_box_from_capsule(capsule.line.a, capsule.line.b, capsule.radius);
  ((GeoCapsule*)prim->data)[prim->count] = capsule;
  ++prim->count;
}

void geo_query_insert_box_rotated(
    GeoQueryEnv* env, const GeoBoxRotated box, const u64 userId, const GeoQueryLayer layer) {
  query_validate_pos(box.box.min);
  query_validate_pos(box.box.max);
  diag_assert_msg(layer, "Shape needs at least one layer");

  QueryPrim* prim = &env->prims[QueryPrimType_BoxRotated];
  prim_ensure_next(prim, QueryPrimType_BoxRotated);
  prim->userIds[prim->count]                = userId;
  prim->layers[prim->count]                 = layer;
  prim->bounds[prim->count]                 = geo_box_from_rotated(&box.box, box.rotation);
  ((GeoBoxRotated*)prim->data)[prim->count] = box;
  ++prim->count;
}

void geo_query_build(GeoQueryEnv* env) {
  bvh_clear(&env->bvh);
  if (!shape_count(env->prims)) {
    return; // Query is empty.
  }
  const u32 rootIndex = bvh_insert_root(&env->bvh, env->prims);
  if (bvh_shape_count(&env->bvh, rootIndex) >= geo_query_bvh_node_divide_threshold) {
    bvh_subdivide(&env->bvh, env->prims, rootIndex);
  }
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

  u32 nodeQueue[32];
  u32 nodeQueueCount = 0;
  if (env->bvh.nodeCount) {
    nodeQueue[nodeQueueCount++] = 0; // Insert root node.
  }

  GeoQueryRayHit bestHit = {.time = f32_max};
  while (nodeQueueCount) {
    const QueryBvhNode* node = &env->bvh.nodes[nodeQueue[--nodeQueueCount]];
    if (!node->shapeCount) {
      // Parent node: Test both child nodes.
      if (bvh_test_ray(&env->bvh, node->childIndex, filter, ray, maxDist)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex;
      }
      if (bvh_test_ray(&env->bvh, node->childIndex + 1, filter, ray, maxDist)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex + 1;
      }
      continue;
    }
    // Leaf node: Test all shapes in the node.
    for (u32 i = 0; i != node->shapeCount; ++i) {
      const QueryShape    shape       = bvh_shape(&env->bvh, node->childIndex + i);
      const GeoQueryLayer shapeLayer  = shape_layer(shape, env->prims);
      const u64           shapeUserId = shape_user_id(shape, env->prims);
      if (!query_filter_layer(filter, shapeLayer)) {
        continue; // Shape layer not included in filter.
      }
      if (!query_filter_callback(filter, shapeUserId, shapeLayer)) {
        continue; // Filtered out by the filter's callback.
      }
      GeoVector normal;
      const f32 hitT = shape_intersect_ray(shape, env->prims, ray, &normal);
      if (hitT < 0.0f || hitT >= bestHit.time) {
        continue; // Miss or a better hit already found.
      }
      // New best hit.
      bestHit.time   = hitT;
      bestHit.userId = shapeUserId;
      bestHit.normal = normal;
      bestHit.layer  = shapeLayer;
    }
  }

  const bool foundHit = bestHit.time <= maxDist;
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

  u32 nodeQueue[32];
  u32 nodeQueueCount = 0;
  if (env->bvh.nodeCount) {
    nodeQueue[nodeQueueCount++] = 0; // Insert root node.
  }

  GeoQueryRayHit bestHit = {.time = f32_max};
  while (nodeQueueCount) {
    const QueryBvhNode* node = &env->bvh.nodes[nodeQueue[--nodeQueueCount]];
    if (!node->shapeCount) {
      // Parent node: Test both child nodes.
      if (bvh_test_ray_fat(&env->bvh, node->childIndex, filter, ray, radius, maxDist)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex;
      }
      if (bvh_test_ray_fat(&env->bvh, node->childIndex + 1, filter, ray, radius, maxDist)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex + 1;
      }
      continue;
    }
    // Leaf node: Test all shapes in the node.
    for (u32 i = 0; i != node->shapeCount; ++i) {
      const QueryShape    shape       = bvh_shape(&env->bvh, node->childIndex + i);
      const GeoQueryLayer shapeLayer  = shape_layer(shape, env->prims);
      const u64           shapeUserId = shape_user_id(shape, env->prims);
      if (!query_filter_layer(filter, shapeLayer)) {
        continue; // Shape layer not included in filter.
      }
      if (!query_filter_callback(filter, shapeUserId, shapeLayer)) {
        continue; // Filtered out by the filter's callback.
      }
      GeoVector normal;
      const f32 hitT = shape_intersect_ray_fat(shape, env->prims, ray, radius, &normal);
      if (hitT < 0.0f || hitT >= bestHit.time) {
        continue; // Miss or a better hit already found.
      }
      // New best hit.
      bestHit.time   = hitT;
      bestHit.userId = shapeUserId;
      bestHit.normal = normal;
      bestHit.layer  = shapeLayer;
    }
  }

  const bool foundHit = bestHit.time <= maxDist;
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

  u32 nodeQueue[32];
  u32 nodeQueueCount = 0;
  if (env->bvh.nodeCount) {
    nodeQueue[nodeQueueCount++] = 0; // Insert root node.
  }

  u32 outCount = 0;
  while (nodeQueueCount) {
    const QueryBvhNode* node = &env->bvh.nodes[nodeQueue[--nodeQueueCount]];
    if (!node->shapeCount) {
      // Parent node: Test both child nodes.
      if (bvh_test_sphere(&env->bvh, node->childIndex, filter, sphere)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex;
      }
      if (bvh_test_sphere(&env->bvh, node->childIndex + 1, filter, sphere)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex + 1;
      }
      continue;
    }
    // Leaf node: Test all shapes in the node.
    for (u32 i = 0; i != node->shapeCount; ++i) {
      const QueryShape    shape       = bvh_shape(&env->bvh, node->childIndex + i);
      const GeoQueryLayer shapeLayer  = shape_layer(shape, env->prims);
      const u64           shapeUserId = shape_user_id(shape, env->prims);
      if (!query_filter_layer(filter, shapeLayer)) {
        continue; // Shape layer not included in filter.
      }
      if (!query_filter_callback(filter, shapeUserId, shapeLayer)) {
        continue; // Filtered out by the filter's callback.
      }
      if (!shape_overlap_sphere(shape, env->prims, sphere)) {
        continue; // Miss.
      }
      // Output hit.
      out[outCount++] = shape_user_id(shape, env->prims);
      if (UNLIKELY(outCount == geo_query_max_hits)) {
        goto MaxCountReached;
      }
    }
  }

MaxCountReached:
  return outCount;
}

u32 geo_query_box_all(
    const GeoQueryEnv*    env,
    const GeoBoxRotated*  boxRotated,
    const GeoQueryFilter* filter,
    u64                   out[PARAM_ARRAY_SIZE(geo_query_max_hits)]) {

  query_stat_add(env, GeoQueryStat_QueryBoxAllCount, 1);

  u32 nodeQueue[32];
  u32 nodeQueueCount = 0;
  if (env->bvh.nodeCount) {
    nodeQueue[nodeQueueCount++] = 0; // Insert root node.
  }

  u32 outCount = 0;
  while (nodeQueueCount) {
    const QueryBvhNode* node = &env->bvh.nodes[nodeQueue[--nodeQueueCount]];
    if (!node->shapeCount) {
      // Parent node: Test both child nodes.
      if (bvh_test_box_rotated(&env->bvh, node->childIndex, filter, boxRotated)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex;
      }
      if (bvh_test_box_rotated(&env->bvh, node->childIndex + 1, filter, boxRotated)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex + 1;
      }
      continue;
    }
    // Leaf node: Test all shapes in the node.
    for (u32 i = 0; i != node->shapeCount; ++i) {
      const QueryShape    shape       = bvh_shape(&env->bvh, node->childIndex + i);
      const GeoQueryLayer shapeLayer  = shape_layer(shape, env->prims);
      const u64           shapeUserId = shape_user_id(shape, env->prims);
      if (!query_filter_layer(filter, shapeLayer)) {
        continue; // Shape layer not included in filter.
      }
      if (!query_filter_callback(filter, shapeUserId, shapeLayer)) {
        continue; // Filtered out by the filter's callback.
      }
      if (!shape_overlap_box_rotated(shape, env->prims, boxRotated)) {
        continue; // Miss.
      }
      // Output hit.
      out[outCount++] = shape_user_id(shape, env->prims);
      if (UNLIKELY(outCount == geo_query_max_hits)) {
        goto MaxCountReached;
      }
    }
  }

MaxCountReached:
  return outCount;
}

u32 geo_query_frustum_all(
    const GeoQueryEnv*    env,
    const GeoVector       frustum[PARAM_ARRAY_SIZE(8)],
    const GeoQueryFilter* filter,
    u64                   out[PARAM_ARRAY_SIZE(geo_query_max_hits)]) {

  query_stat_add(env, GeoQueryStat_QueryFrustumAllCount, 1);

  const GeoBox queryBounds = geo_box_from_frustum(frustum);

  u32 nodeQueue[32];
  u32 nodeQueueCount = 0;
  if (env->bvh.nodeCount) {
    nodeQueue[nodeQueueCount++] = 0; // Insert root node.
  }

  u32 outCount = 0;
  while (nodeQueueCount) {
    const QueryBvhNode* node = &env->bvh.nodes[nodeQueue[--nodeQueueCount]];
    if (!node->shapeCount) {
      // Parent node: Test both child nodes.
      if (bvh_test_box(&env->bvh, node->childIndex, filter, &queryBounds)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex;
      }
      if (bvh_test_box(&env->bvh, node->childIndex + 1, filter, &queryBounds)) {
        diag_assert(nodeQueueCount != array_elems(nodeQueue));
        nodeQueue[nodeQueueCount++] = node->childIndex + 1;
      }
      continue;
    }
    // Leaf node: Test all shapes in the node.
    for (u32 i = 0; i != node->shapeCount; ++i) {
      const QueryShape    shape       = bvh_shape(&env->bvh, node->childIndex + i);
      const GeoQueryLayer shapeLayer  = shape_layer(shape, env->prims);
      const u64           shapeUserId = shape_user_id(shape, env->prims);
      if (!query_filter_layer(filter, shapeLayer)) {
        continue; // Shape layer not included in filter.
      }
      if (!query_filter_callback(filter, shapeUserId, shapeLayer)) {
        continue; // Filtered out by the filter's callback.
      }
      if (!shape_overlap_frustum(shape, env->prims, frustum)) {
        continue; // Miss.
      }
      // Output hit.
      out[outCount++] = shape_user_id(shape, env->prims);
      if (UNLIKELY(outCount == geo_query_max_hits)) {
        goto MaxCountReached;
      }
    }
  }

MaxCountReached:
  return outCount;
}

u32 geo_query_node_count(const GeoQueryEnv* env) { return env->bvh.nodeCount; }

const GeoBox* geo_query_node_bounds(const GeoQueryEnv* env, const u32 index) {
  diag_assert(index < env->bvh.nodeCount);
  return &env->bvh.nodes[index].bounds;
}

u32 geo_query_node_depth(const GeoQueryEnv* env, const u32 index) {
  diag_assert(index < env->bvh.nodeCount);
  return env->bvh.nodes[index].depth;
}

void geo_query_stats_reset(GeoQueryEnv* env) { mem_set(array_mem(env->stats), 0); }

i32* geo_query_stats(GeoQueryEnv* env) {
  env->stats[GeoQueryStat_PrimSphereCount]     = (i32)env->prims[QueryPrimType_Sphere].count;
  env->stats[GeoQueryStat_PrimCapsuleCount]    = (i32)env->prims[QueryPrimType_Capsule].count;
  env->stats[GeoQueryStat_PrimBoxRotatedCount] = (i32)env->prims[QueryPrimType_BoxRotated].count;
  env->stats[GeoQueryStat_BvhNodes]            = (i32)env->bvh.nodeCount;

  u32 maxBvhDepth = 0;
  for (u32 bvhNodeIdx = 0; bvhNodeIdx != env->bvh.nodeCount; ++bvhNodeIdx) {
    maxBvhDepth = math_max(env->bvh.nodes[bvhNodeIdx].depth, maxBvhDepth);
  }
  env->stats[GeoQueryStat_BvhMaxDepth] = maxBvhDepth;

  return env->stats;
}
