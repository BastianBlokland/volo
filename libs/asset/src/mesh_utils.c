#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "geo_matrix.h"

#include "mesh_utils_internal.h"

struct sAssetMeshBuilder {
  DynArray        vertexData; // AssetMeshVertex[]
  DynArray        skinData;   // AssetMeshSkin[]
  DynArray        indexData;  // AssetMeshIndex[]
  AssetMeshIndex* indexTable;
  u32             tableSize, maxVertexCount;
  GeoBox          bounds;
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

void asset_mesh_vertex_transform(AssetMeshVertex* vert, const GeoMatrix* mat) {
  vert->position = geo_matrix_transform3_point(mat, vert->position);
}

void asset_mesh_vertex_quantize(AssetMeshVertex* vert) {
  vert->position = geo_vector_quantize3(vert->position, f16_mantissa_bits);
  vert->normal   = geo_vector_quantize3(vert->normal, f16_mantissa_bits);
  vert->tangent  = geo_vector_quantize(vert->tangent, f16_mantissa_bits);
  vert->texcoord = geo_vector_quantize2(vert->texcoord, f16_mantissa_bits);
}

AssetMeshBuilder* asset_mesh_builder_create(Allocator* alloc, const u32 maxVertexCount) {
  AssetMeshBuilder* builder = alloc_alloc_t(alloc, AssetMeshBuilder);

  *builder = (AssetMeshBuilder){
      .vertexData     = dynarray_create_t(alloc, AssetMeshVertex, maxVertexCount),
      .skinData       = dynarray_create_t(alloc, AssetMeshSkin, 0),
      .indexData      = dynarray_create_t(alloc, AssetMeshIndex, maxVertexCount),
      .tableSize      = bits_nextpow2((u32)maxVertexCount),
      .maxVertexCount = (u32)maxVertexCount,
      .bounds         = geo_box_inverted3(),
      .alloc          = alloc,
  };

  builder->indexTable = alloc_array_t(alloc, AssetMeshIndex, builder->tableSize);
  for (u32 i = 0; i != builder->tableSize; ++i) {
    builder->indexTable[i] = asset_mesh_vertices_max;
  }

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
  builder->bounds = geo_box_inverted3();

  // Reset the index table.
  for (u32 i = 0; i != builder->tableSize; ++i) {
    builder->indexTable[i] = asset_mesh_vertices_max;
  }
}

AssetMeshIndex asset_mesh_builder_push(AssetMeshBuilder* builder, const AssetMeshVertex* vert) {
  /**
   * Deduplicate using a simple open-addressing hash table.
   * https://en.wikipedia.org/wiki/Open_addressing
   */
  const Mem vertMem = mem_create(vert, sizeof(AssetMeshVertex));
  u32       bucket  = bits_hash_32(vertMem) & (builder->tableSize - 1);
  for (usize i = 0; i != builder->tableSize; ++i) {
    AssetMeshIndex* slot = &builder->indexTable[bucket];

    if (LIKELY(*slot == asset_mesh_vertices_max)) {
      diag_assert_msg(
          builder->vertexData.size < builder->maxVertexCount,
          "Vertex count exceeds the maximum capacity {} of the index-type",
          fmt_int(asset_mesh_vertices_max - 1));

      // Unique vertex, copy to output and save the index in the table.
      *slot = (AssetMeshIndex)builder->vertexData.size;
      *dynarray_push_t(&builder->vertexData, AssetMeshVertex) = *vert;
      *dynarray_push_t(&builder->indexData, AssetMeshIndex)   = *slot;

      builder->bounds = geo_box_encapsulate(&builder->bounds, vert->position);
      return *slot;
    }

    diag_assert(*slot < builder->vertexData.size);
    if (mem_eq(dynarray_at(&builder->vertexData, *slot, 1), vertMem)) {
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

AssetMeshComp asset_mesh_create(const AssetMeshBuilder* builder) {
  diag_assert_msg(builder->indexData.size, "Empty mesh is invalid");

  const u32  vertCount  = (u32)builder->vertexData.size;
  const u32  indexCount = (u32)builder->indexData.size;
  const bool isSkinned  = builder->skinData.size != 0;
  diag_assert(!isSkinned || builder->skinData.size == vertCount);

  const usize vertexDataSize = sizeof(AssetMeshVertexPacked) * vertCount;
  const Mem   vertexData = alloc_alloc(g_allocHeap, vertexDataSize, alignof(AssetMeshVertexPacked));

  AssetMeshVertex*       vertsIn  = dynarray_begin_t(&builder->vertexData, AssetMeshVertex);
  AssetMeshSkin*         skinsIn  = dynarray_begin_t(&builder->skinData, AssetMeshSkin);
  AssetMeshVertexPacked* vertsOut = vertexData.ptr;
  if (isSkinned) {
    for (usize i = 0; i != vertCount; ++i) {
      geo_vector_pack_f16(vertsIn[i].position, vertsOut[i].data1);
      geo_vector_pack_f16(vertsIn[i].normal, vertsOut[i].data2);
      vertsOut[i].data1[3] = float_f32_to_f16(vertsIn[i].texcoord.x);
      vertsOut[i].data2[3] = float_f32_to_f16(vertsIn[i].texcoord.y);

      geo_vector_pack_f16(vertsIn[i].tangent, vertsOut[i].data3);

      const u16 w0 = (u8)(skinsIn[i].weights.x * 255.999f);
      const u16 w1 = (u8)(skinsIn[i].weights.y * 255.999f);
      const u16 w2 = (u8)(skinsIn[i].weights.z * 255.999f);
      const u16 w3 = (u8)(skinsIn[i].weights.w * 255.999f);

      vertsOut[i].data4[0] = (u16)skinsIn[i].joints[0] | (w0 << 8);
      vertsOut[i].data4[1] = (u16)skinsIn[i].joints[1] | (w1 << 8);
      vertsOut[i].data4[2] = (u16)skinsIn[i].joints[2] | (w2 << 8);
      vertsOut[i].data4[3] = (u16)skinsIn[i].joints[3] | (w3 << 8);
    }
  } else {
    for (usize i = 0; i != vertCount; ++i) {
      geo_vector_pack_f16(vertsIn[i].position, vertsOut[i].data1);
      geo_vector_pack_f16(vertsIn[i].normal, vertsOut[i].data2);
      vertsOut[i].data1[3] = float_f32_to_f16(vertsIn[i].texcoord.x);
      vertsOut[i].data2[3] = float_f32_to_f16(vertsIn[i].texcoord.y);

      geo_vector_pack_f16(vertsIn[i].tangent, vertsOut[i].data3);

      mem_set(mem_var(vertsOut[i].data4), 0);
    }
  }

  const usize indexDataSize = sizeof(AssetMeshIndex) * indexCount;
  const Mem   indexData     = alloc_alloc(g_allocHeap, indexDataSize, sizeof(AssetMeshIndex));
  mem_cpy(indexData, dynarray_at(&builder->indexData, 0, indexCount));

  return (AssetMeshComp){
      .vertexCount = vertCount,
      .indexCount  = indexCount,
      .vertexData  = data_mem_create(vertexData),
      .indexData   = data_mem_create(indexData),
      .bounds      = builder->bounds,
  };
}

GeoVector asset_mesh_tri_norm(const GeoVector a, const GeoVector b, const GeoVector c) {
  const GeoVector surface = geo_vector_cross3(geo_vector_sub(c, a), geo_vector_sub(b, a));
  if (UNLIKELY(geo_vector_mag_sqr(surface) <= f32_epsilon)) {
    return geo_forward; // Triangle with zero area has technically no normal.
  }
  return geo_vector_norm_exact(surface);
}

void asset_mesh_compute_flat_normals(AssetMeshBuilder* builder) {
  diag_assert_msg(builder->indexData.size, "Empty mesh is invalid");

  /**
   * Compute flat normals (pointing away from the triangle face). This operation potentially needs
   * to split vertices, therefore we take a snapshot of the mesh and then rebuild it.
   */

  AssetMeshSnapshot snapshot = asset_mesh_snapshot(builder, g_allocHeap);
  asset_mesh_builder_clear(builder);

  diag_assert((snapshot.indexCount % 3) == 0); // Input has to be triangles.
  for (u32 i = 0; i != snapshot.indexCount; i += 3) {
    AssetMeshVertex* vA = &snapshot.vertexData[snapshot.indexData[i + 0]];
    AssetMeshVertex* vB = &snapshot.vertexData[snapshot.indexData[i + 1]];
    AssetMeshVertex* vC = &snapshot.vertexData[snapshot.indexData[i + 2]];

    const GeoVector norm      = asset_mesh_tri_norm(vA->position, vB->position, vC->position);
    const GeoVector normQuant = geo_vector_quantize3(norm, f16_mantissa_bits);

    vA->normal                = normQuant;
    const AssetMeshIndex idxA = asset_mesh_builder_push(builder, vA);

    vB->normal                = normQuant;
    const AssetMeshIndex idxB = asset_mesh_builder_push(builder, vB);

    vC->normal                = normQuant;
    const AssetMeshIndex idxC = asset_mesh_builder_push(builder, vC);

    if (snapshot.skinData) {
      // Preserve the original skinning.
      asset_mesh_builder_set_skin(builder, idxA, snapshot.skinData[snapshot.indexData[i + 0]]);
      asset_mesh_builder_set_skin(builder, idxB, snapshot.skinData[snapshot.indexData[i + 1]]);
      asset_mesh_builder_set_skin(builder, idxC, snapshot.skinData[snapshot.indexData[i + 2]]);
    }
  }

  alloc_free(g_allocHeap, snapshot.mem);
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

  Mem bufferMem = alloc_alloc(g_allocHeap, 2 * vertCount * sizeof(GeoVector), alignof(GeoVector));
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
    const GeoVector orthoTanRaw = geo_vector_sub(t, geo_vector_project(t, n));
    if (geo_vector_mag_sqr(orthoTanRaw) <= f32_epsilon) {
      // Not possible to calculate a tangent, tangent and normal are opposite of each-other.
      vertices[i].tangent = geo_vector(1, 0, 0, 1);
      continue;
    }

    GeoVector orthoTan = geo_vector_norm_exact(orthoTanRaw);

    // Calculate the 'handedness', aka if the bi-tangent needs to be flipped.
    orthoTan.w = (geo_vector_dot(geo_vector_cross3(n, t), b) < 0) ? 1.f : -1.f;

    vertices[i].tangent = orthoTan;
  }

  alloc_free(g_allocHeap, bufferMem);
}
