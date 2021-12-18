#pragma once
#include "asset_mesh.h"

#include "buffer_internal.h"
#include "transfer_internal.h"

// Internal forward declarations:
typedef struct sRvkCanvas RvkCanvas;
typedef struct sRvkDevice RvkDevice;

typedef struct sRvkMesh {
  RvkDevice*    dev;
  u32           vertexCount, indexCount;
  RvkBuffer     vertexBuffer, indexBuffer;
  RvkTransferId vertexTransfer, indexTransfer;
} RvkMesh;

RvkMesh* rvk_mesh_create(RvkDevice*, const AssetMeshComp*);
void     rvk_mesh_destroy(RvkMesh*);
bool     rvk_mesh_prepare(RvkMesh*, const RvkCanvas*);
