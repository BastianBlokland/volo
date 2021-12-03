#include "asset_mesh.h"
#include "core_dynarray.h"
#include "ecs_world.h"

#include "repo_internal.h"

/**
 * Wavefront Obj.
 * Polygonal faces are supported (no curves or lines), materials are also ignored at this time.
 * Format specification: http://www.martinreddy.net/gfx/3d/OBJ.spec
 * Faces are assumed to be convex and a triangulated using a simple triangle fan.
 */

/**
 * Indices for a single face vertex.
 * These are already bounds checked and converted to absolute indices starting from 0.
 * Normal and texcoord are optional, 'sentinel_u32' means unused.
 */
typedef struct {
  u32 positionIdx;
  u32 normalIdx;
  u32 texcoordIdx;
} ObjVertex;

/**
 * Obj face.
 * Contains three or more vertices, no upper bound on amount of vertices.
 */
typedef struct {
  u32 vertexIdx;
  u32 vertexCount;

  /**
   * Indicates that the surface normal should be used instead of per vertex, happens if not all
   * vertices define a vertex normal.
   */
  bool useFaceNormal;
} ObjFace;

typedef struct {
  DynArray positions; // GeoVector[]
  DynArray texcoords; // GeoVector[]
  DynArray normals;   // GeoVector[]
  DynArray vertices;  // ObjVertex[]
  DynArray faces;     // ObjFace[]
  u32      totalTris;
} ObjData;

void asset_load_obj(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  (void)world;
  (void)assetEntity;
  (void)src;
}
