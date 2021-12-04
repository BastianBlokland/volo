#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_sentinel.h"

#include "mesh_utils_internal.h"

struct sAssetMeshBuilder {
  DynArray   vertices; // AssetMeshVertex[]
  DynArray   indices;  // u16[]
  u16*       indexTable;
  usize      tableSize, maxVertexCount;
  Allocator* alloc;
};

AssetMeshBuilder* asset_mesh_builder_create(Allocator* alloc, const usize maxVertexCount) {
  AssetMeshBuilder* builder = alloc_alloc_t(alloc, AssetMeshBuilder);
  *builder                  = (AssetMeshBuilder){
      .vertices       = dynarray_create_t(alloc, AssetMeshVertex, maxVertexCount),
      .indices        = dynarray_create_t(alloc, u16, maxVertexCount),
      .maxVertexCount = maxVertexCount,
      .tableSize      = bits_nextpow2(maxVertexCount),
      .alloc          = alloc,
  };

  builder->indexTable = alloc_array_t(alloc, u16, builder->tableSize);
  for (usize i = 0; i != builder->tableSize; ++i) {
    builder->indexTable[i] = sentinel_u16;
  }

  diag_assert_msg(
      maxVertexCount < u16_max,
      "Vertex count {} exceeds the maximum capacity {} of the index-type",
      fmt_int(maxVertexCount),
      fmt_int(u16_max - 1));
  return builder;
}

void asset_mesh_builder_destroy(AssetMeshBuilder* builder) {
  dynarray_destroy(&builder->vertices);
  dynarray_destroy(&builder->indices);
  alloc_free_array_t(builder->alloc, builder->indexTable, builder->tableSize);

  alloc_free_t(builder->alloc, builder);
}

u16 asset_mesh_builder_push(AssetMeshBuilder* builder, const AssetMeshVertex vertex) {
  diag_assert_msg(
      builder->vertices.size < builder->maxVertexCount, "Vertex count exceeds the maximum");

  /**
   * Deduplicate using a simple open-addressing hash table.
   * https://en.wikipedia.org/wiki/Open_addressing
   */
  u32 bucket = bits_hash_32(mem_var(vertex)) & (builder->tableSize - 1);
  for (usize i = 0; i != builder->tableSize; ++i) {
    u16* slot = &builder->indexTable[bucket];

    if (LIKELY(sentinel_check(*slot))) {
      // Unique vertex, copy to output and save the index in the table.
      *slot                                                 = builder->vertices.size;
      *dynarray_push_t(&builder->vertices, AssetMeshVertex) = vertex;
      *dynarray_push_t(&builder->indices, u16)              = *slot;
      return *slot;
    }

    diag_assert(*slot < builder->vertices.size);
    if (mem_eq(dynarray_at(&builder->vertices, *slot, 1), mem_var(vertex))) {
      // Equal to the vertex in this slot, reuse the vertex.
      *dynarray_push_t(&builder->indices, u16) = *slot;
      return *slot;
    }

    // Hash collision, jump to a new place in the table (quadratic probing).
    bucket = (bucket + i + 1) & (builder->tableSize - 1);
  }
  diag_crash_msg("Mesh index table full");
}

AssetMeshComp asset_mesh_create(const AssetMeshBuilder* builder) {
  const usize vertCount = builder->vertices.size;
  const Mem   vertMem =
      alloc_alloc(g_alloc_heap, vertCount * sizeof(AssetMeshVertex), alignof(AssetMeshVertex));
  mem_cpy(vertMem, dynarray_at(&builder->vertices, 0, vertCount));

  const usize idxCount   = builder->indices.size;
  const Mem   indicesMem = alloc_alloc(g_alloc_heap, idxCount * sizeof(u16), alignof(u16));
  mem_cpy(indicesMem, dynarray_at(&builder->indices, 0, idxCount));

  return (AssetMeshComp){
      .vertices    = vertMem.ptr,
      .vertexCount = vertCount,
      .indices     = vertMem.ptr,
      .indexCount  = idxCount,
  };
}
