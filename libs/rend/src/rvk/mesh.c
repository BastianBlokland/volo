#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "log_logger.h"

#include "device_internal.h"
#include "mesh_internal.h"
#include "transfer_internal.h"

#define rvk_mesh_vertex_align 16

typedef struct {
  ALIGNAS(16)
  f16 data1[4]; // x, y, z position
  f16 data2[4]; // x, y texcoord
  f16 data3[4]; // x, y, z normal
  f16 data4[4]; // x, y, z tangent, w tangent handedness
} RvkVertexPacked;

ASSERT(sizeof(RvkVertexPacked) == 32, "Unexpected vertex size");
ASSERT(alignof(RvkVertexPacked) == rvk_mesh_vertex_align, "Unexpected vertex alignment");

typedef struct {
  ALIGNAS(16)
  f16 data1[4]; // x, y, z position
  f16 data2[4]; // x, y texcoord
  f16 data3[4]; // x, y, z normal
  f16 data4[4]; // x, y, z tangent, w tangent handedness
  u16 data5[4]; // x jointIndex0, y jointIndex1, z jointIndex2, w jointIndex3,
  f16 data6[4]; // x jointWeight0, y jointWeight1, z jointWeight2, w jointWeight3
} RvkVertexSkinnedPacked;

ASSERT(sizeof(RvkVertexSkinnedPacked) == 48, "Unexpected vertex size");
ASSERT(alignof(RvkVertexSkinnedPacked) == rvk_mesh_vertex_align, "Unexpected vertex alignment");

/**
 * Compatible with the structures defined in 'vertex.glsl' using the std140 glsl layout.
 */

#define rvk_mesh_max_scratch_size (64 * usize_kibibyte)

static bool rvk_mesh_skinned(const AssetMeshComp* asset) { return asset->skinData != null; }

static u32 rvk_mesh_vertex_size(const AssetMeshComp* asset) {
  return rvk_mesh_skinned(asset) ? sizeof(RvkVertexSkinnedPacked) : sizeof(RvkVertexPacked);
}

static Mem rvk_mesh_to_device_vertices(Allocator* alloc, const AssetMeshComp* asset) {
  diag_assert_msg(asset->vertexCount, "Mesh asset does not contain any vertices");

  const usize bufferSize = rvk_mesh_vertex_size(asset) * asset->vertexCount;
  Mem         buffer     = alloc_alloc(alloc, bufferSize, rvk_mesh_vertex_align);

  if (rvk_mesh_skinned(asset)) {
    RvkVertexSkinnedPacked* output = mem_as_t(buffer, RvkVertexSkinnedPacked);
    for (usize i = 0; i != asset->vertexCount; ++i) {
      geo_vector_pack_f16(asset->vertexData[i].position, output[i].data1);
      geo_vector_pack_f16(asset->vertexData[i].texcoord, output[i].data2);
      geo_vector_pack_f16(asset->vertexData[i].normal, output[i].data3);
      geo_vector_pack_f16(asset->vertexData[i].tangent, output[i].data4);

      output[i].data5[0] = asset->skinData[i].joints[0];
      output[i].data5[1] = asset->skinData[i].joints[1];
      output[i].data5[2] = asset->skinData[i].joints[2];
      output[i].data5[3] = asset->skinData[i].joints[3];

      geo_vector_pack_f16(asset->skinData[i].weights, output[i].data6);
    }
  } else {
    RvkVertexPacked* output = mem_as_t(buffer, RvkVertexPacked);
    for (usize i = 0; i != asset->vertexCount; ++i) {
      geo_vector_pack_f16(asset->vertexData[i].position, output[i].data1);
      geo_vector_pack_f16(asset->vertexData[i].texcoord, output[i].data2);
      geo_vector_pack_f16(asset->vertexData[i].normal, output[i].data3);
      geo_vector_pack_f16(asset->vertexData[i].tangent, output[i].data4);
    }
  }
  return buffer;
}

RvkMesh* rvk_mesh_create(RvkDevice* dev, const AssetMeshComp* asset, const String dbgName) {
  RvkMesh* mesh = alloc_alloc_t(g_alloc_heap, RvkMesh);
  *mesh         = (RvkMesh){
      .device      = dev,
      .dbgName     = string_dup(g_alloc_heap, dbgName),
      .vertexCount = (u32)asset->vertexCount,
      .indexCount  = (u32)asset->indexCount,
  };

  const u32  vertexSize    = rvk_mesh_vertex_size(asset);
  const bool useScratch    = vertexSize * asset->vertexCount < rvk_mesh_max_scratch_size;
  Allocator* verticesAlloc = useScratch ? g_alloc_scratch : g_alloc_heap;
  const Mem  verticesMem   = rvk_mesh_to_device_vertices(verticesAlloc, asset);

  const usize indexSize = sizeof(AssetMeshIndex) * asset->indexCount;
  mesh->vertexBuffer    = rvk_buffer_create(dev, verticesMem.size, RvkBufferType_DeviceStorage);
  mesh->indexBuffer     = rvk_buffer_create(dev, indexSize, RvkBufferType_DeviceIndex);

  rvk_debug_name_buffer(dev->debug, mesh->vertexBuffer.vkBuffer, "{}_vertex", fmt_text(dbgName));
  rvk_debug_name_buffer(dev->debug, mesh->indexBuffer.vkBuffer, "{}_index", fmt_text(dbgName));

  mesh->vertexTransfer = rvk_transfer_buffer(dev->transferer, &mesh->vertexBuffer, verticesMem);
  mesh->indexTransfer  = rvk_transfer_buffer(
      dev->transferer, &mesh->indexBuffer, mem_create(asset->indexData, indexSize));

  alloc_free(verticesAlloc, verticesMem);

  log_d(
      "Vulkan mesh created",
      log_param("name", fmt_text(dbgName)),
      log_param("skinned", fmt_bool(rvk_mesh_skinned(asset))),
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

  log_d("Vulkan mesh destroyed", log_param("name", fmt_text(mesh->dbgName)));

  string_free(g_alloc_heap, mesh->dbgName);
  alloc_free_t(g_alloc_heap, mesh);
}

bool rvk_mesh_prepare(RvkMesh* mesh) {
  if (mesh->flags & RvkMeshFlags_Ready) {
    return true;
  }
  RvkDevice* dev = mesh->device;
  if (!rvk_transfer_poll(dev->transferer, mesh->vertexTransfer)) {
    return false;
  }
  if (!rvk_transfer_poll(dev->transferer, mesh->indexTransfer)) {
    return false;
  }
  // All resources have been transferred to the device.
  mesh->flags |= RvkMeshFlags_Ready;
  return true;
}
