#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
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

void asset_mesh_compute_tangents(AssetMeshBuilder* builder) {

  /**
   * Calculate a tangent and bitangent per triangle and accumlate the results per vertex. At the end
   * we compute a tangent per vertex by averaging the tangent and bitangents, this has the effect of
   * smoothing the tangents for vertices that are shared by multiple triangles.
   */

  const usize vertCount = builder->vertices.size;
  const usize idxCount  = builder->indices.size;

  Mem bufferMem = alloc_alloc(g_alloc_heap, 2 * vertCount * sizeof(GeoVector), alignof(GeoVector));
  mem_set(bufferMem, 0);

  GeoVector* tangents   = bufferMem.ptr;
  GeoVector* bitangents = tangents + vertCount;

  AssetMeshVertex* vertices = dynarray_at_t(&builder->vertices, 0, AssetMeshVertex);
  const u16*       indices  = dynarray_at_t(&builder->indices, 0, u16);

  // Calculate per triangle tangents and bitangents and accumulate them per vertex.
  diag_assert((builder->indices.size % 3) == 0); // Input has to be triangles.
  for (usize i = 0; i != idxCount; i += 3) {
    const AssetMeshVertex* vA = &vertices[indices[i]];
    const AssetMeshVertex* vB = &vertices[indices[i + 1]];
    const AssetMeshVertex* vC = &vertices[indices[i + 2]];

    const GeoVector deltaPos1 = geo_vector_sub(vB->position, vA->position);
    const GeoVector deltaPos2 = geo_vector_sub(vC->position, vA->position);
    const GeoVector deltaTex1 = geo_vector_sub(vB->texcoord, vA->texcoord);
    const GeoVector deltaTex2 = geo_vector_sub(vC->texcoord, vA->texcoord);

    const f32 s = deltaTex1.x * deltaTex2.y - deltaTex2.x * deltaTex1.y;
    if (math_abs(s) <= f32_epsilon) {
      // Not possible to calculate a tangent/bitangent here, triangle has zero texcoord area.
      continue;
    }

    const GeoVector pos1Tex2Y = geo_vector_mul(deltaPos1, deltaTex2.y);
    const GeoVector pos2Tex1Y = geo_vector_mul(deltaPos2, deltaTex1.y);
    const GeoVector tan       = geo_vector_div(geo_vector_sub(pos1Tex2Y, pos2Tex1Y), s);

    tangents[indices[i]]     = geo_vector_add(tangents[indices[i]], tan);
    tangents[indices[i + 1]] = geo_vector_add(tangents[indices[i + 1]], tan);
    tangents[indices[i + 2]] = geo_vector_add(tangents[indices[i + 2]], tan);

    const GeoVector pos1Tex2X = geo_vector_mul(deltaPos1, deltaTex2.x);
    const GeoVector pos2Tex1X = geo_vector_mul(deltaPos2, deltaTex1.x);
    const GeoVector bitan     = geo_vector_div(geo_vector_sub(pos2Tex1X, pos1Tex2X), s);

    bitangents[indices[i]]     = geo_vector_add(bitangents[indices[i]], bitan);
    bitangents[indices[i + 1]] = geo_vector_add(bitangents[indices[i + 1]], bitan);
    bitangents[indices[i + 2]] = geo_vector_add(bitangents[indices[i + 2]], bitan);
  }

  // Write the tangents to the vertices vector.
  for (usize i = 0; i != vertCount; ++i) {
    const GeoVector t = tangents[i];        // tangent.
    const GeoVector b = bitangents[i];      // bitangent.
    const GeoVector n = vertices[i].normal; // normal.
    if (geo_vector_mag_sqr(t) <= f32_epsilon) {
      // Not possible to calculate a tangent, vertex is not used in any triangle with non-zero
      // positional area and texcoord area.
      vertices[i].tangent = geo_vector(1, 0, 0, 1);
      continue;
    }

    // Ortho-normalize the tangent in case the texcoords are skewed.
    GeoVector orthoTan = geo_vector_norm(geo_vector_sub(t, geo_vector_project(t, n)));

    // Calculate the 'handedness', aka if the bi-tangent needs to be flipped.
    orthoTan.w = (geo_vector_dot(geo_vector_cross3(n, t), b) < 0) ? 1.f : -1.f;

    vertices[i].tangent = orthoTan;
  }

  alloc_free(g_alloc_heap, bufferMem);
}
