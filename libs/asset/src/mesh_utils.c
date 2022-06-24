#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "core_sentinel.h"

#include "mesh_utils_internal.h"

struct sAssetMeshBuilder {
  DynArray        vertexData; // AssetMeshVertex[]
  DynArray        skinData;   // AssetMeshSkin[]
  DynArray        indexData;  // AssetMeshIndex[]
  AssetMeshIndex* indexTable;
  u32             tableSize, maxVertexCount;
  GeoBox          positionBounds, texcoordBounds;
  Allocator*      alloc;
};

typedef struct {
  Mem              mem;
  AssetMeshVertex* vertexData;
  AssetMeshSkin*   skinData;
  AssetMeshIndex*  indexData;
  u32              vertexCount, indexCount;
} AssetMeshSnapshot;

static AssetMeshSnapshot asset_mesh_snapshot(const AssetMeshBuilder* builder, Allocator* alloc) {
  diag_assert_msg(builder->indexData.size, "Cannot take a snapshot of an empty mesh");

  enum { Align = 16 };
  ASSERT(alignof(AssetMeshVertex) <= Align, "Mesh vertex requires too strong alignment");
  ASSERT(alignof(AssetMeshSkin) <= Align, "Mesh skin requires too strong alignment");

  const Mem orgVertMem = dynarray_at(&builder->vertexData, 0, builder->vertexData.size);
  const Mem orgSkinMem = dynarray_at(&builder->skinData, 0, builder->skinData.size);
  const Mem orgIdxMem  = dynarray_at(&builder->indexData, 0, builder->indexData.size);

  const usize memSize = bits_align(orgVertMem.size + orgSkinMem.size + orgIdxMem.size, Align);
  const Mem   mem     = alloc_alloc(alloc, memSize, Align);
  const Mem   vertMem = mem_slice(mem, 0, orgVertMem.size);
  const Mem   skinMem = mem_slice(mem, orgVertMem.size, orgSkinMem.size);
  const Mem   idxMem  = mem_slice(mem, orgVertMem.size + orgSkinMem.size, orgIdxMem.size);

  mem_cpy(vertMem, orgVertMem);
  mem_cpy(skinMem, orgSkinMem);
  mem_cpy(idxMem, orgIdxMem);

  return (AssetMeshSnapshot){
      .mem         = mem,
      .vertexData  = mem_as_t(vertMem, AssetMeshVertex),
      .skinData    = skinMem.size ? mem_as_t(skinMem, AssetMeshSkin) : null,
      .indexData   = mem_as_t(idxMem, AssetMeshIndex),
      .vertexCount = (u32)builder->vertexData.size,
      .indexCount  = (u32)builder->indexData.size,
  };
}

AssetMeshBuilder* asset_mesh_builder_create(Allocator* alloc, const u32 maxVertexCount) {
  AssetMeshBuilder* builder = alloc_alloc_t(alloc, AssetMeshBuilder);

  *builder = (AssetMeshBuilder){
      .vertexData     = dynarray_create_t(alloc, AssetMeshVertex, maxVertexCount),
      .skinData       = dynarray_create_t(alloc, AssetMeshSkin, 0),
      .indexData      = dynarray_create_t(alloc, AssetMeshIndex, maxVertexCount),
      .tableSize      = bits_nextpow2((u32)maxVertexCount),
      .maxVertexCount = (u32)maxVertexCount,
      .positionBounds = geo_box_inverted3(),
      .texcoordBounds = geo_box_inverted2(),
      .alloc          = alloc,
  };

  builder->indexTable = alloc_array_t(alloc, AssetMeshIndex, builder->tableSize);
  for (u32 i = 0; i != builder->tableSize; ++i) {
    builder->indexTable[i] = asset_mesh_indices_max;
  }

  diag_assert_msg(
      maxVertexCount < asset_mesh_indices_max,
      "Vertex count {} exceeds the maximum capacity {} of the index-type",
      fmt_int(maxVertexCount),
      fmt_int(asset_mesh_indices_max - 1));
  return builder;
}

void asset_mesh_builder_destroy(AssetMeshBuilder* builder) {
  dynarray_destroy(&builder->vertexData);
  dynarray_destroy(&builder->skinData);
  dynarray_destroy(&builder->indexData);
  alloc_free_array_t(builder->alloc, builder->indexTable, builder->tableSize);

  alloc_free_t(builder->alloc, builder);
}

void asset_mesh_builder_clear(AssetMeshBuilder* builder) {
  dynarray_clear(&builder->vertexData);
  dynarray_clear(&builder->skinData);
  dynarray_clear(&builder->indexData);
  builder->positionBounds = geo_box_inverted3();
  builder->texcoordBounds = geo_box_inverted3();

  // Reset the index table.
  for (u32 i = 0; i != builder->tableSize; ++i) {
    builder->indexTable[i] = asset_mesh_indices_max;
  }
}

AssetMeshIndex asset_mesh_builder_push(AssetMeshBuilder* builder, const AssetMeshVertex vertex) {
  /**
   * Deduplicate using a simple open-addressing hash table.
   * https://en.wikipedia.org/wiki/Open_addressing
   */
  u32 bucket = bits_hash_32(mem_var(vertex)) & (builder->tableSize - 1);
  for (usize i = 0; i != builder->tableSize; ++i) {
    AssetMeshIndex* slot = &builder->indexTable[bucket];

    if (LIKELY(*slot == asset_mesh_indices_max)) {
      diag_assert_msg(
          builder->vertexData.size < builder->maxVertexCount, "Vertex count exceeds the maximum");

      // Unique vertex, copy to output and save the index in the table.
      *slot = (AssetMeshIndex)builder->vertexData.size;
      *dynarray_push_t(&builder->vertexData, AssetMeshVertex) = vertex;
      *dynarray_push_t(&builder->indexData, AssetMeshIndex)   = *slot;

      builder->positionBounds = geo_box_encapsulate(&builder->positionBounds, vertex.position);
      builder->texcoordBounds = geo_box_encapsulate2(&builder->texcoordBounds, vertex.texcoord);
      return *slot;
    }

    diag_assert(*slot < builder->vertexData.size);
    if (mem_eq(dynarray_at(&builder->vertexData, *slot, 1), mem_var(vertex))) {
      // Equal to the vertex in this slot, reuse the vertex.
      *dynarray_push_t(&builder->indexData, AssetMeshIndex) = *slot;
      return *slot;
    }

    // Hash collision, jump to a new place in the table (quadratic probing).
    bucket = (bucket + i + 1) & (builder->tableSize - 1);
  }
  diag_crash_msg("Mesh index table full");
}

void asset_mesh_builder_set_skin(
    AssetMeshBuilder* builder, const AssetMeshIndex idx, const AssetMeshSkin skin) {
  /**
   * NOTE: This makes the assumption that vertices can never be split based on skinning alone. So
   * there cannot be vertices with identical position/norm/texcoord but different skinning.
   */
  dynarray_resize(&builder->skinData, builder->vertexData.size);
  *dynarray_at_t(&builder->skinData, idx, AssetMeshSkin) = skin;
}

void asset_mesh_builder_override_bounds(AssetMeshBuilder* builder, const GeoBox overrideBounds) {
  builder->positionBounds = overrideBounds;
}

AssetMeshComp asset_mesh_create(const AssetMeshBuilder* builder) {
  diag_assert_msg(builder->indexData.size, "Empty mesh is invalid");
  diag_assert(!builder->skinData.size || builder->skinData.size == builder->vertexData.size);

  const u32 vertCount = (u32)builder->vertexData.size;
  const u32 idxCount  = (u32)builder->indexData.size;

  return (AssetMeshComp){
      .vertexData     = dynarray_copy_as_new(&builder->vertexData, g_alloc_heap),
      .skinData       = dynarray_copy_as_new(&builder->skinData, g_alloc_heap),
      .vertexCount    = vertCount,
      .indexData      = dynarray_copy_as_new(&builder->indexData, g_alloc_heap),
      .indexCount     = idxCount,
      .positionBounds = builder->positionBounds,
      .texcoordBounds = builder->texcoordBounds,
  };
}

GeoVector asset_mesh_tri_norm(const GeoVector a, const GeoVector b, const GeoVector c) {
  const GeoVector surface = geo_vector_cross3(geo_vector_sub(c, a), geo_vector_sub(b, a));
  if (UNLIKELY(geo_vector_mag_sqr(surface) <= f32_epsilon)) {
    return geo_forward; // Triangle with zero area has technically no normal.
  }
  return geo_vector_norm(surface);
}

void asset_mesh_compute_flat_normals(AssetMeshBuilder* builder) {
  diag_assert_msg(builder->indexData.size, "Empty mesh is invalid");

  /**
   * Compute flat normals (pointing away from the triangle face). This operation potentially needs
   * to split vertices, therefore we take a snapshot of the mesh and then rebuild it.
   */

  AssetMeshSnapshot snapshot = asset_mesh_snapshot(builder, g_alloc_heap);
  asset_mesh_builder_clear(builder);

  diag_assert((snapshot.indexCount % 3) == 0); // Input has to be triangles.
  for (u32 i = 0; i != snapshot.indexCount; i += 3) {
    AssetMeshVertex* vA = &snapshot.vertexData[snapshot.indexData[i + 0]];
    AssetMeshVertex* vB = &snapshot.vertexData[snapshot.indexData[i + 1]];
    AssetMeshVertex* vC = &snapshot.vertexData[snapshot.indexData[i + 2]];

    const GeoVector norm      = asset_mesh_tri_norm(vA->position, vB->position, vC->position);
    const GeoVector normQuant = geo_vector_quantize3(norm, 20);

    vA->normal                = normQuant;
    const AssetMeshIndex idxA = asset_mesh_builder_push(builder, *vA);

    vB->normal                = normQuant;
    const AssetMeshIndex idxB = asset_mesh_builder_push(builder, *vB);

    vC->normal                = normQuant;
    const AssetMeshIndex idxC = asset_mesh_builder_push(builder, *vC);

    if (snapshot.skinData) {
      // Preserve the original skinning.
      asset_mesh_builder_set_skin(builder, idxA, snapshot.skinData[snapshot.indexData[i + 0]]);
      asset_mesh_builder_set_skin(builder, idxB, snapshot.skinData[snapshot.indexData[i + 1]]);
      asset_mesh_builder_set_skin(builder, idxC, snapshot.skinData[snapshot.indexData[i + 2]]);
    }
  }

  alloc_free(g_alloc_heap, snapshot.mem);
}

void asset_mesh_compute_tangents(AssetMeshBuilder* builder) {
  diag_assert_msg(builder->indexData.size, "Empty mesh is invalid");

  /**
   * Calculate a tangent and bi-tangent per triangle and accumulate the results per vertex. At the
   * end we compute a tangent per vertex by averaging the tangent and bi-tangents, this has the
   * effect of smoothing the tangents for vertices that are shared by multiple triangles.
   */

  const usize vertCount = builder->vertexData.size;
  const usize idxCount  = builder->indexData.size;

  Mem bufferMem = alloc_alloc(g_alloc_heap, 2 * vertCount * sizeof(GeoVector), alignof(GeoVector));
  mem_set(bufferMem, 0);

  GeoVector* tangents   = bufferMem.ptr;
  GeoVector* bitangents = tangents + vertCount;

  AssetMeshVertex*      vertices = dynarray_begin_t(&builder->vertexData, AssetMeshVertex);
  const AssetMeshIndex* indices  = dynarray_begin_t(&builder->indexData, AssetMeshIndex);

  // Calculate per triangle tangents and bi-tangents and accumulate them per vertex.
  diag_assert((idxCount % 3) == 0); // Input has to be triangles.
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
      // Not possible to calculate a tangent/bi-tangent here, triangle has zero texcoord area.
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
    const GeoVector b = bitangents[i];      // bi-tangent.
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
