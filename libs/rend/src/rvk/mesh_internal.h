#pragma once
#include "asset_mesh.h"

#include "buffer_internal.h"
#include "transfer_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkPass   RvkPass;

typedef enum {
  RvkMeshFlags_Ready = 1 << 0,
} RvkMeshFlags;

typedef struct sRvkMesh {
  RvkDevice*    device;
  String        dbgName;
  RvkMeshFlags  flags;
  u32           vertexCount, indexCount;
  RvkBuffer     vertexBuffer, indexBuffer;
  RvkTransferId vertexTransfer, indexTransfer;
} RvkMesh;

RvkMesh* rvk_mesh_create(RvkDevice*, const AssetMeshComp*, String dbgName);
void     rvk_mesh_destroy(RvkMesh*);
bool     rvk_mesh_prepare(RvkMesh*);
