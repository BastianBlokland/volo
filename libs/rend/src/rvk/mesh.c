#include "core_alloc.h"
#include "core_diag.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "device_internal.h"
#include "mesh_internal.h"
#include "transfer_internal.h"

typedef struct {
  f32 pos[3];
  f32 padding[1];
} RvkVertex;

static Mem rvk_mesh_to_device_vertices_scratch(const AssetMeshComp* asset) {
  const usize bufferSize = sizeof(RvkVertex) * asset->vertexCount;
  Mem         buffer     = alloc_alloc(g_alloc_scratch, bufferSize, alignof(RvkVertex));

  RvkVertex* output = mem_as_t(buffer, RvkVertex);
  for (usize i = 0; i != asset->vertexCount; ++i) {
    output[i] = (RvkVertex){
        .pos[0] = asset->vertices[i].position.x,
        .pos[1] = asset->vertices[i].position.y,
        .pos[2] = asset->vertices[i].position.z,
    };
  }
  return buffer;
}

RvkMesh* rvk_mesh_create(RvkDevice* dev, const AssetMeshComp* asset) {
  RvkMesh* mesh = alloc_alloc_t(g_alloc_heap, RvkMesh);
  *mesh         = (RvkMesh){
      .dev         = dev,
      .vertexCount = (u32)asset->vertexCount,
      .indexCount  = (u32)asset->indexCount,
  };

  const Mem vertices = rvk_mesh_to_device_vertices_scratch(asset);

  const usize indexSize = sizeof(u16) * asset->indexCount;
  mesh->vertexBuffer    = rvk_buffer_create(dev, vertices.size, RvkBufferType_DeviceStorage);
  mesh->indexBuffer     = rvk_buffer_create(dev, indexSize, RvkBufferType_DeviceIndex);

  mesh->vertexTransfer = rvk_transfer_buffer(dev->transferer, &mesh->vertexBuffer, vertices);
  mesh->indexTransfer  = rvk_transfer_buffer(
      dev->transferer, &mesh->indexBuffer, mem_create(asset->indices, indexSize));

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

bool rvk_mesh_prepare(RvkMesh* mesh, const RvkCanvas* canvas) {
  (void)canvas;

  if (!rvk_transfer_poll(mesh->dev->transferer, mesh->vertexTransfer)) {
    return false;
  }
  if (!rvk_transfer_poll(mesh->dev->transferer, mesh->indexTransfer)) {
    return false;
  }
  return true; // All resources have been transferred to the device.
}
