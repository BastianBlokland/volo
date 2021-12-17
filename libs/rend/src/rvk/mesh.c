#include "core_alloc.h"
#include "core_diag.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "device_internal.h"
#include "mesh_internal.h"

typedef struct {
  f32 pos[3];
  f32 padding[1];
} RvkVertex;

RvkMesh* rvk_mesh_create(RvkDevice* dev, const AssetMeshComp* asset) {
  RvkMesh* mesh = alloc_alloc_t(g_alloc_heap, RvkMesh);
  *mesh         = (RvkMesh){
      .dev         = dev,
      .vertexCount = (u32)asset->vertexCount,
      .indexCount  = (u32)asset->indexCount,
  };

  const usize vertexSize = sizeof(RvkVertex) * asset->vertexCount;
  const usize indexSize  = sizeof(u16) * asset->indexCount;
  mesh->vertexBuffer     = rvk_buffer_create(dev, vertexSize, RvkBufferType_DeviceStorage);
  mesh->indexBuffer      = rvk_buffer_create(dev, indexSize, RvkBufferType_DeviceIndex);

  log_d(
      "Vulkan mesh created",
      log_param("vertices", fmt_int(mesh->vertexCount)),
      log_param("indices", fmt_int(mesh->indexCount)),
      log_param("vertexMemory", fmt_size(mesh->vertexBuffer.mem.size)),
      log_param("indexMemory", fmt_size(mesh->indexBuffer.mem.size)));

  return mesh;
}

void rvk_mesh_destroy(RvkMesh* mesh) {

  rvk_buffer_destroy(&mesh->vertexBuffer);
  rvk_buffer_destroy(&mesh->indexBuffer);

  log_d("Vulkan mesh destroyed");

  alloc_free_t(g_alloc_heap, mesh);
}
