#pragma once
#include "asset_mesh.h"

#include "buffer_internal.h"
#include "transfer_internal.h"

// Internal forward declarations:
typedef struct sRvkPlatform RvkPlatform;
typedef struct sRvkPass     RvkPass;

typedef struct sRvkMesh {
  RvkPlatform*  platform;
  u32           vertexCount, indexCount;
  RvkBuffer     vertexBuffer, indexBuffer;
  RvkTransferId vertexTransfer, indexTransfer;
} RvkMesh;

RvkMesh* rvk_mesh_create(RvkPlatform*, const AssetMeshComp*);
void     rvk_mesh_destroy(RvkMesh*);
bool     rvk_mesh_prepare(RvkMesh*, const RvkPass*);
