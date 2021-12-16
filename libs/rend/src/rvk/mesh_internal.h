#pragma once
#include "asset_mesh.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef struct sRvkMesh {
  RvkDevice* dev;
} RvkMesh;

RvkMesh* rvk_mesh_create(RvkDevice*, const AssetMeshComp*);
void     rvk_mesh_destroy(RvkMesh*);
