#pragma once
#include "asset_mesh.h"

#include "buffer_internal.h"
#include "transfer_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef struct sRvkMesh {
  u32           vertexCount, indexCount;
  RvkBuffer     vertexBuffer, indexBuffer;
  RvkTransferId vertexTransfer, indexTransfer;
  GeoBox        positionBounds, positionRawBounds;
} RvkMesh;

RvkMesh* rvk_mesh_create(RvkDevice*, const AssetMeshComp*, String dbgName);
void     rvk_mesh_destroy(RvkMesh*, RvkDevice*);
bool     rvk_mesh_is_ready(const RvkMesh*, const RvkDevice*);
void     rvk_mesh_bind(const RvkMesh*, const RvkDevice*, VkCommandBuffer);
