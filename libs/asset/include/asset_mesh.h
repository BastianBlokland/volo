#pragma once
#include "ecs_module.h"
#include "geo_box.h"
#include "geo_matrix.h"

#define asset_mesh_indices_max u32_max
#define asset_mesh_joints_max 32
ASSERT(asset_mesh_joints_max <= u8_max, "Joint indices should be representable by a u8");

typedef u32 AssetMeshIndex;

typedef struct {
  GeoVector position; // x, y, z position
  GeoVector normal;   // x, y, z normal
  GeoVector tangent;  // z, y, z tangent, w tangent handedness
  GeoVector texcoord; // x, y texcoord0
} AssetMeshVertex;

typedef struct {
  u8        joints[4]; // joint indices.
  GeoVector weights;   // joint weights.
} AssetMeshSkin;

ecs_comp_extern_public(AssetMeshComp) {
  const AssetMeshVertex* vertexData;
  const AssetMeshSkin*   skinData; // NOTE: null if the mesh has no skinning.
  usize                  vertexCount;
  const AssetMeshIndex*  indexData;
  usize                  indexCount;
  GeoBox                 positionBounds, texcoordBounds;
};

typedef struct {
  GeoMatrix invBindTransform;       // From world to local bind space for a joint.
  u32       childBegin, childCount; // Indices into the 'childIndices' array.
  String    name;
} AssetMeshJoint;

ecs_comp_extern_public(AssetMeshSkeletonComp) {
  u8                    jointCount;
  const AssetMeshJoint* joints;
  const u32*            childIndices;
};
