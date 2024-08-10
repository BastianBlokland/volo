#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "mesh_internal.h"
#include "transfer_internal.h"

#define VOLO_RVK_MESH_LOGGING 0

#define rvk_mesh_vertex_align 16

/**
 * Packed vertex.
 * Compatible with the structure defined in 'vertex.glsl' using the std140 glsl layout.
 */
typedef struct {
  ALIGNAS(16)
  f16 data1[4]; // x, y, z position, w texcoord x
  f16 data2[4]; // x, y, z normal , w texcoord y
  f16 data3[4]; // x, y, z tangent, w tangent handedness
  u16 data4[4]; // x jntIndexWeight0, y jntIndexWeight1, z jntIndexWeight2, w jntIndexWeight3,
} RvkVertexPacked;

ASSERT(sizeof(RvkVertexPacked) == 32, "Unexpected vertex size");
ASSERT(alignof(RvkVertexPacked) == rvk_mesh_vertex_align, "Unexpected vertex alignment");

#define rvk_mesh_max_scratch_size (64 * usize_kibibyte)

static bool rvk_mesh_skinned(const AssetMeshComp* asset) {
  return (asset->flags & AssetMeshFlags_Skinned) != 0;
}

static Mem rvk_mesh_to_device_vertices(Allocator* alloc, const AssetMeshComp* asset) {
  diag_assert_msg(asset->vertices.count, "Mesh asset does not contain any vertices");

  const usize bufferSize = asset->vertices.count * sizeof(RvkVertexPacked);
  Mem         buffer     = alloc_alloc(alloc, bufferSize, rvk_mesh_vertex_align);

  RvkVertexPacked* output = mem_as_t(buffer, RvkVertexPacked);
  if (rvk_mesh_skinned(asset)) {
    for (usize i = 0; i != asset->vertices.count; ++i) {
      geo_vector_pack_f16(asset->vertices.values[i].position, output[i].data1);
      geo_vector_pack_f16(asset->vertices.values[i].normal, output[i].data2);
      output[i].data1[3] = float_f32_to_f16(asset->vertices.values[i].texcoord.x);
      output[i].data2[3] = float_f32_to_f16(asset->vertices.values[i].texcoord.y);

      geo_vector_pack_f16(asset->vertices.values[i].tangent, output[i].data3);

      const u16 w0 = (u8)(asset->skins.values[i].weights.x * 255.999f);
      const u16 w1 = (u8)(asset->skins.values[i].weights.y * 255.999f);
      const u16 w2 = (u8)(asset->skins.values[i].weights.z * 255.999f);
      const u16 w3 = (u8)(asset->skins.values[i].weights.w * 255.999f);

      output[i].data4[0] = (u16)asset->skins.values[i].joints[0] | (w0 << 8);
      output[i].data4[1] = (u16)asset->skins.values[i].joints[1] | (w1 << 8);
      output[i].data4[2] = (u16)asset->skins.values[i].joints[2] | (w2 << 8);
      output[i].data4[3] = (u16)asset->skins.values[i].joints[3] | (w3 << 8);
    }
  } else {
    for (usize i = 0; i != asset->vertices.count; ++i) {
      geo_vector_pack_f16(asset->vertices.values[i].position, output[i].data1);
      geo_vector_pack_f16(asset->vertices.values[i].normal, output[i].data2);
      output[i].data1[3] = float_f32_to_f16(asset->vertices.values[i].texcoord.x);
      output[i].data2[3] = float_f32_to_f16(asset->vertices.values[i].texcoord.y);

      geo_vector_pack_f16(asset->vertices.values[i].tangent, output[i].data3);
    }
  }
  return buffer;
}

RvkMesh* rvk_mesh_create(RvkDevice* dev, const AssetMeshComp* asset, const String dbgName) {
  RvkMesh* mesh = alloc_alloc_t(g_allocHeap, RvkMesh);
  *mesh         = (RvkMesh){
      .device            = dev,
      .dbgName           = string_dup(g_allocHeap, dbgName),
      .vertexCount       = (u32)asset->vertices.count,
      .indexCount        = (u32)asset->indices.count,
      .positionBounds    = asset->positionBounds,
      .positionRawBounds = asset->positionRawBounds,
  };

  if (rvk_mesh_skinned(asset)) {
    mesh->flags |= RvkMeshFlags_Skinned;
  }

  const u32  vertexSize    = sizeof(RvkVertexPacked);
  const bool useScratch    = vertexSize * asset->vertices.count < rvk_mesh_max_scratch_size;
  Allocator* verticesAlloc = useScratch ? g_allocScratch : g_allocHeap;
  const Mem  verticesMem   = rvk_mesh_to_device_vertices(verticesAlloc, asset);

  const usize indexSize = sizeof(AssetMeshIndex) * asset->indices.count;
  mesh->vertexBuffer    = rvk_buffer_create(dev, verticesMem.size, RvkBufferType_DeviceStorage);
  mesh->indexBuffer     = rvk_buffer_create(dev, indexSize, RvkBufferType_DeviceIndex);

  rvk_debug_name_buffer(dev->debug, mesh->vertexBuffer.vkBuffer, "{}_vertex", fmt_text(dbgName));
  rvk_debug_name_buffer(dev->debug, mesh->indexBuffer.vkBuffer, "{}_index", fmt_text(dbgName));

  mesh->vertexTransfer = rvk_transfer_buffer(dev->transferer, &mesh->vertexBuffer, verticesMem);
  mesh->indexTransfer  = rvk_transfer_buffer(
      dev->transferer, &mesh->indexBuffer, mem_create(asset->indices.values, indexSize));

  alloc_free(verticesAlloc, verticesMem);

#if VOLO_RVK_MESH_LOGGING
  log_d(
      "Vulkan mesh created",
      log_param("name", fmt_text(dbgName)),
      log_param("skinned", fmt_bool((mesh->flags & RvkMeshFlags_Skinned) != 0)),
      log_param("vertices", fmt_int(mesh->vertexCount)),
      log_param("indices", fmt_int(mesh->indexCount)),
      log_param("vertex-memory", fmt_size(mesh->vertexBuffer.mem.size)),
      log_param("index-memory", fmt_size(mesh->indexBuffer.mem.size)));
#endif

  return mesh;
}

void rvk_mesh_destroy(RvkMesh* mesh) {
  RvkDevice* dev = mesh->device;
  rvk_buffer_destroy(&mesh->vertexBuffer, dev);
  rvk_buffer_destroy(&mesh->indexBuffer, dev);

#if VOLO_RVK_MESH_LOGGING
  log_d("Vulkan mesh destroyed", log_param("name", fmt_text(mesh->dbgName)));
#endif

  string_free(g_allocHeap, mesh->dbgName);
  alloc_free_t(g_allocHeap, mesh);
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
