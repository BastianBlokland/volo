#pragma once
#include "asset_mesh.h"

#include "buffer_internal.h"
#include "transfer_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef enum {
  RvkMeshFlags_Skinned = 1 << 0,
} RvkMeshFlags;

typedef struct sRvkMesh {
  RvkDevice*    device;
  RvkMeshFlags  flags;
  u32           vertexCount, indexCount;
  RvkBuffer     vertexBuffer, indexBuffer;
  RvkTransferId vertexTransfer, indexTransfer;
  GeoBox        positionBounds, positionRawBounds;
} RvkMesh;

RvkMesh* rvk_mesh_create(RvkDevice*, const AssetMeshComp*, String dbgName);
void     rvk_mesh_destroy(RvkMesh*);
bool     rvk_mesh_is_ready(const RvkMesh*);
void     rvk_mesh_bind(const RvkMesh*, VkCommandBuffer);
