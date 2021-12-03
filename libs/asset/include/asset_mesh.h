#pragma once
#include "ecs_module.h"
#include "geo_vector.h"

typedef struct {
  GeoVector position; // x, y, z position
  GeoVector normal;   // x, y, z normal
  GeoVector tangent;  // z, y, z tangent, w tangent handedness
  GeoVector texcoord; // x, y texcoord1
} AssetMeshVertex;

ecs_comp_extern_public(AssetMeshComp) {
  const AssetMeshVertex* vertices;
  usize                  vertexCount;
  const u16*             indices;
  usize                  indexCount;
};
