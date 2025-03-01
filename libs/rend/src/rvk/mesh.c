#include "core_diag.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "lib_internal.h"
#include "mesh_internal.h"
#include "transfer_internal.h"

#define VOLO_RVK_MESH_LOGGING 0
#define VOLO_RVK_MESH_VALIDATE_BIND 0

RvkMesh* rvk_mesh_create(RvkDevice* dev, const AssetMeshComp* asset, const String dbgName) {
  RvkMesh* mesh = alloc_alloc_t(g_allocHeap, RvkMesh);

  *mesh = (RvkMesh){
      .vertexCount = asset->vertexCount,
      .indexCount  = asset->indexCount,
      .bounds      = asset->bounds,
  };

  const Mem vertexMem = data_mem(asset->vertexData);
  const Mem indexMem  = data_mem(asset->indexData);

  mesh->vertexBuffer = rvk_buffer_create(dev, vertexMem.size, RvkBufferType_DeviceStorage);
  mesh->indexBuffer  = rvk_buffer_create(dev, indexMem.size, RvkBufferType_DeviceIndex);

  rvk_debug_name_buffer(dev->debug, mesh->vertexBuffer.vkBuffer, "{}_vertex", fmt_text(dbgName));
  rvk_debug_name_buffer(dev->debug, mesh->indexBuffer.vkBuffer, "{}_index", fmt_text(dbgName));

  mesh->vertexTransfer = rvk_transfer_buffer(dev->transferer, &mesh->vertexBuffer, vertexMem);
  mesh->indexTransfer  = rvk_transfer_buffer(dev->transferer, &mesh->indexBuffer, indexMem);

#if VOLO_RVK_MESH_LOGGING
  log_d(
      "Vulkan mesh created",
      log_param("name", fmt_text(dbgName)),
      log_param("vertices", fmt_int(mesh->vertexCount)),
      log_param("indices", fmt_int(mesh->indexCount)),
      log_param("vertex-memory", fmt_size(mesh->vertexBuffer.mem.size)),
      log_param("index-memory", fmt_size(mesh->indexBuffer.mem.size)));
#endif

  return mesh;
}

void rvk_mesh_destroy(RvkMesh* mesh, RvkDevice* dev) {
  rvk_buffer_destroy(&mesh->vertexBuffer, dev);
  rvk_buffer_destroy(&mesh->indexBuffer, dev);

#if VOLO_RVK_MESH_LOGGING
  log_d("Vulkan mesh destroyed");
#endif

  alloc_free_t(g_allocHeap, mesh);
}

bool rvk_mesh_is_ready(const RvkMesh* mesh, const RvkDevice* dev) {
  if (!rvk_transfer_poll(dev->transferer, mesh->vertexTransfer)) {
    return false;
  }
  if (!rvk_transfer_poll(dev->transferer, mesh->indexTransfer)) {
    return false;
  }
  return true;
}

void rvk_mesh_bind(const RvkMesh* mesh, const RvkDevice* dev, VkCommandBuffer vkCmdBuf) {
  (void)dev;
#if VOLO_RVK_MESH_VALIDATE_BIND
  diag_assert(rvk_mesh_is_ready(mesh, dev));
#endif

  VkIndexType indexType;
  if (sizeof(AssetMeshIndex) == 2) {
    indexType = VK_INDEX_TYPE_UINT16;
  } else {
    indexType = VK_INDEX_TYPE_UINT32;
  }

  rvk_call(dev, cmdBindIndexBuffer, vkCmdBuf, mesh->indexBuffer.vkBuffer, 0, indexType);
}
