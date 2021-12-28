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
  GeoVector texcoord; // x, y texcoord1
} AssetMeshVertex;

ecs_comp_extern_public(AssetMeshComp) {
  const AssetMeshVertex* vertices;
  usize                  vertexCount;
  const AssetMeshIndex*  indices;
  usize                  indexCount;
  GeoBox                 positionBounds, texcoordBounds;
};
