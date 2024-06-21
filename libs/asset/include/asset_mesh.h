#pragma once
#include "ecs_module.h"
#include "geo_box.h"
#include "geo_matrix.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

#define asset_mesh_vertices_max u16_max
#define asset_mesh_joints_max 75
ASSERT(asset_mesh_joints_max <= u8_max, "Joint indices should be representable by a u8");

typedef u16 AssetMeshIndex;
typedef u32 AssetMeshAnimPtr;

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
  const AssetMeshIndex*  indexData;
  u32                    vertexCount;
  u32                    indexCount;
  GeoBox                 positionBounds;
  GeoBox                 positionRawBounds; // Unscaled (does not take skinning into account).
  GeoBox                 texcoordBounds;
};

typedef enum {
  AssetMeshAnimTarget_Translation,
  AssetMeshAnimTarget_Rotation,
  AssetMeshAnimTarget_Scale,

  AssetMeshAnimTarget_Count,
} AssetMeshAnimTarget;

typedef struct {
  u32              frameCount;
  AssetMeshAnimPtr timeData;  // u16[frameCount] (normalized, fractions of the anim duration).
  AssetMeshAnimPtr valueData; // (GeoVector | GeoQuat)[frameCount].
} AssetMeshAnimChannel;

typedef struct {
  StringHash           nameHash;
  f32                  duration;
  AssetMeshAnimChannel joints[asset_mesh_joints_max][AssetMeshAnimTarget_Count];
} AssetMeshAnim;

ecs_comp_extern_public(AssetMeshSkeletonComp) {
  const AssetMeshAnim* anims;
  AssetMeshAnimPtr     bindPoseInvMats; // GeoMatrix[jointCount]. From world to local bind space.
  AssetMeshAnimPtr     defaultPose;     // (GeoVector | GeoQuat)[jointCount][3]. Local TRS.
  AssetMeshAnimPtr     rootTransform;   // (GeoVector | GeoQuat)[3]. // TRS.
  AssetMeshAnimPtr     parentIndices;   // u32[jointCount].
  AssetMeshAnimPtr     skinCounts;      // u32[jointCount]. Amount of verts skinned to each joint.
  AssetMeshAnimPtr     jointNames;      // StringHash[jointCount].
  u8                   jointCount;
  u32                  animCount;
  Mem                  animData; // 16 bit aligned and the size is always a multiple of 16.
};

void asset_mesh_proc_jsonschema_write(DynString*);
