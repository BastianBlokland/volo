#pragma once
#include "asset_mesh.h"

#include "buffer_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef struct sRvkMesh {
  RvkDevice* dev;
  u32        vertexCount, indexCount;
  RvkBuffer  vertexBuffer;
  RvkBuffer  indexBuffer;
} RvkMesh;

RvkMesh* rvk_mesh_create(RvkDevice*, const AssetMeshComp*);
void     rvk_mesh_destroy(RvkMesh*);
