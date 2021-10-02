#include "core_alloc.h"
#include "core_diag.h"

#include "archetype_internal.h"
#include "def_internal.h"

/**
 * An archetype is a chunked container, where every chunk contains a tightly packed component-data
 * array for each component.
 * Chunks are created on-demand as more entities get added, but only destroyed when the archetype is
 * destroyed.
 *
 * Chunk memory layout:
 * ```
 * | EcsEntityId | [ALIGN PADDING] HealthComp | [ALIGN PADDING] PositionComp |
 * |-------------|----------------------------|------------------------------|
 * | 1           | { health = 42 }            | { x: 2, y: -34 }             |
 * | 2           | { health = 1337 }          | { x: 1, y: 9 }               |
 * ```
 */

#define ecs_archetype_chunk_size 8192
#define ecs_archetype_max_chunks 128

static void* ecs_archetype_chunk_create() {
  const usize align = 512; // Note: In practice the page allocator will align to the page size.
  return alloc_alloc(g_alloc_page, ecs_archetype_chunk_size, align).ptr;
}

static void ecs_archetype_chunk_destroy(void* chunk) {
  alloc_free(g_alloc_page, mem_create(chunk, ecs_archetype_chunk_size));
}

static usize ecs_archetype_comp_idx(EcsArchetype* archetype, const EcsCompId id) {
  return bitset_index(archetype->mask, id);
}

static void ecs_archetype_copy(EcsArchetype* archetype, const u32 dst, const u32 src) {
  const u16* compOffsets = archetype->compOffsetsAndStrides;
  const u16* compStrides = archetype->compOffsetsAndStrides + archetype->compCount;

  const usize dstChunkIdx     = dst / archetype->entitiesPerChunk;
  const usize dstIndexInChunk = dst - (dstChunkIdx * archetype->entitiesPerChunk);
  u8*         dstChunk        = archetype->chunks[dstChunkIdx];

  const usize srcChunkIdx     = src / archetype->entitiesPerChunk;
  const usize srcIndexInChunk = src - (srcChunkIdx * archetype->entitiesPerChunk);
  u8*         srcChunk        = archetype->chunks[srcChunkIdx];

  for (usize compIdx = 0; compIdx != archetype->compCount; ++compIdx) {
    const usize compOffset       = compOffsets[compIdx];
    const usize compSize         = compStrides[compIdx];
    u8*         dstChunkCompData = bits_ptr_offset(dstChunk, compOffset);
    u8*         srcChunkCompData = bits_ptr_offset(srcChunk, compOffset);

    Mem dstCompMem = mem_create(dstChunkCompData + compSize * dstIndexInChunk, compSize);
    Mem srcCompMem = mem_create(srcChunkCompData + compSize * srcIndexInChunk, compSize);

    mem_cpy(dstCompMem, srcCompMem);
  }
}

EcsArchetype ecs_archetype_create(const EcsDef* def, BitSet mask) {
  diag_assert_msg(bitset_any(mask), "Archetype needs to contain atleast a single component");

  usize compCount      = 0;
  usize entityDataSize = sizeof(EcsEntityId);
  usize padding        = 0;
  bitset_for(mask, compId, {
    ++compCount;
    const usize compSize  = ecs_def_comp_size(def, (EcsCompId)compId);
    const usize compAlign = ecs_def_comp_align(def, (EcsCompId)compId);
    padding += bits_padding(entityDataSize + padding, compAlign);
    entityDataSize += compSize;
  });

  const usize entitiesPerChunk = (ecs_archetype_chunk_size - padding) / entityDataSize;
  diag_assert_msg(entitiesPerChunk, "At least one entity has to fit in an archetype chunk");

  u16* compOffsets = alloc_alloc(g_alloc_heap, sizeof(u16) * compCount * 2, alignof(u16)).ptr;
  u16* compStrides = compOffsets + compCount;

  usize offset  = sizeof(EcsEntityId) * entitiesPerChunk;
  usize compIdx = 0;
  bitset_for(mask, compId, {
    const usize compSize  = ecs_def_comp_size(def, (EcsCompId)compId);
    const usize compAlign = ecs_def_comp_align(def, (EcsCompId)compId);
    offset                = bits_align(offset, compAlign);
    compOffsets[compIdx]  = (u16)offset;
    compStrides[compIdx]  = (u16)compSize;
    offset += compSize * entitiesPerChunk;
  });

  return (EcsArchetype){
      .mask                  = alloc_dup(g_alloc_heap, mask, 1),
      .entitiesPerChunk      = entitiesPerChunk,
      .compOffsetsAndStrides = compOffsets,
      .compCount             = compCount,
      .chunks =
          alloc_alloc(g_alloc_heap, sizeof(void*) * ecs_archetype_max_chunks, alignof(void*)).ptr,
  };
}

void ecs_archetype_destroy(EcsArchetype* archetype) {
  alloc_free(g_alloc_heap, archetype->mask);

  alloc_free(
      g_alloc_heap,
      mem_create(archetype->compOffsetsAndStrides, sizeof(u16) * archetype->compCount * 2));

  for (usize chunkIdx = 0; chunkIdx != archetype->chunkCount; ++chunkIdx) {
    ecs_archetype_chunk_destroy(archetype->chunks[chunkIdx]);
  }
  alloc_free(g_alloc_heap, mem_create(archetype->chunks, sizeof(void*) * ecs_archetype_max_chunks));
}

u32 ecs_archetype_add(EcsArchetype* archetype, const EcsEntityId id) {
  if (archetype->entityCount == archetype->chunkCount * archetype->entitiesPerChunk) {
    // Not a enough space left; allocate a new chunk.
    diag_assert(archetype->chunkCount < ecs_archetype_max_chunks);
    archetype->chunks[archetype->chunkCount++] = ecs_archetype_chunk_create();
  }
  // TODO: Add check to detect overflowing a u32 entity-index.
  const u32 entityIdx                         = (u32)(archetype->entityCount++);
  *ecs_archetype_entity(archetype, entityIdx) = id;
  return entityIdx;
}

EcsEntityId ecs_archetype_remove(EcsArchetype* archetype, const u32 index) {
  const u32 lastIndex = archetype->entityCount - 1;
  if (index == lastIndex) {
    --archetype->entityCount;
    return 0;
  }

  /**
   * This is not the last entry meaning we get a hole when we remove it.
   * To fix this up we copy the last entity into that hole.
   */

  const EcsEntityId entityToMove = *ecs_archetype_entity(archetype, lastIndex);
  ecs_archetype_copy(archetype, index, lastIndex);
  --archetype->entityCount;
  return entityToMove;
}

EcsEntityId* ecs_archetype_entity(EcsArchetype* archetype, const u32 index) {
  const usize chunkIdx     = index / archetype->entitiesPerChunk;
  const usize indexInChunk = index - (chunkIdx * archetype->entitiesPerChunk);
  return (EcsEntityId*)archetype->chunks[chunkIdx] + indexInChunk;
}

void* ecs_archetype_comp(EcsArchetype* archetype, const u32 index, const EcsCompId id) {
  /**
   * Random access to a specific component on the entity at the given index.
   *
   * Steps:
   * - Find the chunk this entity is in (and the index it has in that chunk).
   * - Find the offset into this chunk where the requested component array starts.
   * - Return a pointer into the component array at 'indexInChunk'.
   */
  const u16* compOffsets = archetype->compOffsetsAndStrides;
  const u16* compStrides = archetype->compOffsetsAndStrides + archetype->compCount;

  const usize compIdx       = ecs_archetype_comp_idx(archetype, id);
  const usize chunkIdx      = index / archetype->entitiesPerChunk;
  const usize indexInChunk  = index - (chunkIdx * archetype->entitiesPerChunk);
  u8*         chunk         = archetype->chunks[chunkIdx];
  u8*         chunkCompData = bits_ptr_offset(chunk, compOffsets[compIdx]);
  return chunkCompData + compStrides[compIdx] * indexInChunk;
}
