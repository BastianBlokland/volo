#pragma once
#include "ecs_module.h"
#include "geo_box.h"
#include "geo_vector.h"

#define asset_mesh_indices_max u32_max

typedef u32 AssetMeshIndex;

typedef struct {
  GeoVector position; // x, y, z position
  GeoVector normal;   // x, y, z normal
  GeoVector tangent;  // z, y, z tangent, w tangent handedness
  GeoVector texcoord; // x, y texcoord0
} AssetMeshVertex;

typedef struct {
  u8  bones[4];   // bone indices.
  f32 weights[4]; // bone weights.
} AssetMeshSkin;

ecs_comp_extern_public(AssetMeshComp) {
  const AssetMeshVertex* vertexData;
  const AssetMeshSkin*   skinData; // NOTE: null if the mesh has no skinning.
  usize                  vertexCount;
  const AssetMeshIndex*  indexData;
  usize                  indexCount;
  GeoBox                 positionBounds, texcoordBounds;
};
