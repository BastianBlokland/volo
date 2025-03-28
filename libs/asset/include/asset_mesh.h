#pragma once
#include "core_array.h"
#include "data_registry.h"
#include "ecs_module.h"
#include "geo_box.h"

#define asset_mesh_vertices_max u16_max
#define asset_mesh_joints_max 75
#define asset_mesh_anims_max 32

ASSERT(asset_mesh_joints_max <= u8_max, "Joint indices should be representable by a u8");

typedef u16 AssetMeshIndex;
typedef u32 AssetMeshDataPtr;

/**
 * Packed vertex.
 * Compatible with the structure defined in 'vertex.glsl' using the std140 glsl layout.
 */
typedef struct {
  ALIGNAS(16)
  f16 data1[4]; // x, y, z position, w texcoord x
  f16 data2[4]; // x, y, z normal , w texcoord y
  f16 data3[4]; // x, y, z tangent, w tangent handedness
  u16 data4[4]; // x jntIndexWeight0, y jntIndexWeight1, z jntIndexWeight2, w jntIndexWeight3,
} AssetMeshVertexPacked;

ASSERT(sizeof(AssetMeshVertexPacked) == 32, "Unexpected vertex size");
ASSERT(alignof(AssetMeshVertexPacked) == 16, "Unexpected vertex alignment");

ecs_comp_extern_public(AssetMeshComp) {
  u32     vertexCount, indexCount;
  DataMem vertexData; // AssetMeshVertexPacked[]
  DataMem indexData;  // AssetMeshIndex[]
  GeoBox  bounds;
};

typedef enum {
  AssetMeshAnimFlags_Active     = 1 << 0,
  AssetMeshAnimFlags_Loop       = 1 << 1,
  AssetMeshAnimFlags_FadeIn     = 1 << 2,
  AssetMeshAnimFlags_FadeOut    = 1 << 3,
  AssetMeshAnimFlags_RandomTime = 1 << 4,
} AssetMeshAnimFlags;

typedef enum {
  AssetMeshAnimTarget_Translation,
  AssetMeshAnimTarget_Rotation,
  AssetMeshAnimTarget_Scale,

  AssetMeshAnimTarget_Count,
} AssetMeshAnimTarget;

typedef struct {
  u32              frameCount;
  AssetMeshDataPtr timeData;  // u16[frameCount] (normalized, fractions of the anim duration).
  AssetMeshDataPtr valueData; // (GeoVector | GeoQuat)[frameCount].
} AssetMeshAnimChannel;

typedef struct {
  String               name; // Interned.
  AssetMeshAnimFlags   flags;
  f32                  duration, time, speedMin, speedMax, weight;
  AssetMeshAnimChannel joints[asset_mesh_joints_max][AssetMeshAnimTarget_Count];
  f32                  mask[asset_mesh_joints_max];
} AssetMeshAnim;

ecs_comp_extern_public(AssetMeshSkeletonComp) {
  HeapArray_t(AssetMeshAnim) anims;
  AssetMeshDataPtr bindMatInv;      // GeoMatrix[jointCount]. From world to bind space.
  AssetMeshDataPtr defaultPose;     // (GeoVector | GeoQuat)[jointCount][3]. Local TRS.
  AssetMeshDataPtr rootTransform;   // (GeoVector | GeoQuat)[3]. // TRS.
  AssetMeshDataPtr parentIndices;   // u32[jointCount].
  AssetMeshDataPtr skinCounts;      // u32[jointCount]. Amount of verts skinned to each joint.
  AssetMeshDataPtr boundingRadius;  // f32[jointCount]. Bounding sphere radius for each joint.
  AssetMeshDataPtr jointNameHashes; // StringHash[jointCount].
  AssetMeshDataPtr jointNames;      // struct { u8 size; u8 data[size]; }[jointCount].
  u8               jointCount;
  DataMem          data; // 16 bit aligned and the size is always a multiple of 16.
};

extern DataMeta g_assetMeshBundleMeta;
extern DataMeta g_assetMeshMeta;
extern DataMeta g_assetMeshSkeletonMeta;
extern DataMeta g_assetProcMeshDefMeta;
