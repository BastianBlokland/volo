#include "core_alloc.h"
#include "core_diag.h"
#include "log_logger.h"

#include "device_internal.h"
#include "mesh_internal.h"

RvkMesh* rvk_mesh_create(RvkDevice* dev, const AssetMeshComp* asset) {
  RvkMesh* mesh = alloc_alloc_t(g_alloc_heap, RvkMesh);
  *mesh         = (RvkMesh){
      .dev = dev,
  };

  log_d(
      "Vulkan mesh created",
      log_param("vertices", fmt_int(asset->vertexCount)),
      log_param("indices", fmt_int(asset->indexCount)));

  return mesh;
}

void rvk_mesh_destroy(RvkMesh* mesh) {

  log_d("Vulkan mesh destroyed");

  alloc_free_t(g_alloc_heap, mesh);
}
