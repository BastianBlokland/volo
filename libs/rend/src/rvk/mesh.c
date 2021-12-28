#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "log_logger.h"

#include "device_internal.h"
#include "mesh_internal.h"
#include "transfer_internal.h"

typedef struct {
  f16 position[3];
  f16 pad_0[1];
  f16 texcoord[2];
  f16 pad_1[2];
} RvkVertex;

#define rvk_mesh_max_scratch_size (64 * usize_kibibyte)

static Mem rvk_mesh_to_device_vertices(Allocator* alloc, const AssetMeshComp* asset) {
  const usize bufferSize = sizeof(RvkVertex) * asset->vertexCount;
  Mem         buffer     = alloc_alloc(alloc, bufferSize, alignof(RvkVertex));

  RvkVertex* output = mem_as_t(buffer, RvkVertex);
  for (usize i = 0; i != asset->vertexCount; ++i) {
    output[i] = (RvkVertex){
        .position[0] = bits_f32_to_f16(asset->vertices[i].position.x),
        .position[1] = bits_f32_to_f16(asset->vertices[i].position.y),
        .position[2] = bits_f32_to_f16(asset->vertices[i].position.z),
        .texcoord[0] = bits_f32_to_f16(asset->vertices[i].texcoord.x),
        .texcoord[1] = bits_f32_to_f16(asset->vertices[i].texcoord.y),
    };
  }
  return buffer;
}

RvkMesh* rvk_mesh_create(RvkDevice* dev, const AssetMeshComp* asset, const String dbgName) {
  RvkMesh* mesh = alloc_alloc_t(g_alloc_heap, RvkMesh);
  *mesh         = (RvkMesh){
      .device      = dev,
      .vertexCount = (u32)asset->vertexCount,
      .indexCount  = (u32)asset->indexCount,
  };

  const bool useScratch    = sizeof(RvkVertex) * asset->vertexCount < rvk_mesh_max_scratch_size;
  Allocator* verticesAlloc = useScratch ? g_alloc_scratch : g_alloc_heap;
  const Mem  verticesMem   = rvk_mesh_to_device_vertices(verticesAlloc, asset);

  const usize indexSize = sizeof(u16) * asset->indexCount;
  mesh->vertexBuffer    = rvk_buffer_create(dev, verticesMem.size, RvkBufferType_DeviceStorage);
  mesh->indexBuffer     = rvk_buffer_create(dev, indexSize, RvkBufferType_DeviceIndex);

  rvk_debug_name_buffer(dev->debug, mesh->vertexBuffer.vkBuffer, "{}_vertex", fmt_text(dbgName));
  rvk_debug_name_buffer(dev->debug, mesh->indexBuffer.vkBuffer, "{}_index", fmt_text(dbgName));

  mesh->vertexTransfer = rvk_transfer_buffer(dev->transferer, &mesh->vertexBuffer, verticesMem);
  mesh->indexTransfer  = rvk_transfer_buffer(
      dev->transferer, &mesh->indexBuffer, mem_create(asset->indices, indexSize));

  alloc_free(verticesAlloc, verticesMem);

  log_d(
      "Vulkan mesh created",
      log_param("vertices", fmt_int(mesh->vertexCount)),
      log_param("indices", fmt_int(mesh->indexCount)),
      log_param("vertex-memory", fmt_size(mesh->vertexBuffer.mem.size)),
      log_param("index-memory", fmt_size(mesh->indexBuffer.mem.size)));

  return mesh;
}

void rvk_mesh_destroy(RvkMesh* mesh) {

  RvkDevice* dev = mesh->device;
  rvk_buffer_destroy(&mesh->vertexBuffer, dev);
  rvk_buffer_destroy(&mesh->indexBuffer, dev);

  alloc_free_t(g_alloc_heap, mesh);
}

bool rvk_mesh_prepare(RvkMesh* mesh) {
  RvkDevice* dev = mesh->device;

  if (!rvk_transfer_poll(dev->transferer, mesh->vertexTransfer)) {
    return false;
  }
  if (!rvk_transfer_poll(dev->transferer, mesh->indexTransfer)) {
    return false;
  }
  return true; // All resources have been transferred to the device.
}
