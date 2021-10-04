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

typedef struct {
  u32 chunkIdx;
  u32 indexInChunk;
} EcsArchetypeLoc;

static u32 ecs_archetype_entities_per_chunk(const EcsDef* def, BitSet mask) {
  /**
   * Calculate how much total array space each entity will take + how much padding there will need
   * to be between the arrays to satisfy the component alignments.
   */
  usize entityDataSize = sizeof(EcsEntityId);
  usize padding        = 0;
  bitset_for(mask, compId, {
    const usize compSize  = ecs_def_comp_size(def, (EcsCompId)compId);
    const usize compAlign = ecs_def_comp_align(def, (EcsCompId)compId);
    padding += bits_padding(entityDataSize + padding, compAlign);
    entityDataSize += compSize;
  });
  return (u32)((ecs_archetype_chunk_size - padding) / entityDataSize);
}

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

static EcsEntityId* ecs_archetype_entity_ptr(EcsArchetype* archetype, const u32 index) {
  const usize chunkIdx     = index / archetype->entitiesPerChunk;
  const usize indexInChunk = index - (chunkIdx * archetype->entitiesPerChunk);
  return (EcsEntityId*)archetype->chunks[chunkIdx] + indexInChunk;
}

static EcsArchetypeLoc ecs_archetype_location(EcsArchetype* archetype, const u32 index) {
  const u32 chunkIdx     = index / archetype->entitiesPerChunk;
  const u32 indexInChunk = index - (chunkIdx * archetype->entitiesPerChunk);
  return (EcsArchetypeLoc){.chunkIdx = chunkIdx, .indexInChunk = indexInChunk};
}

static void
ecs_archetype_itr_init_pointers(EcsArchetype* archetype, EcsIterator* itr, EcsArchetypeLoc loc) {
  const u16* compOffsets = archetype->compOffsetsAndSizes;
  const u16* compSizes   = archetype->compOffsetsAndSizes + archetype->compCount;
  u8*        chunkData   = archetype->chunks[loc.chunkIdx];

  itr->chunkIdx = loc.chunkIdx;
  itr->entity   = ((EcsEntityId*)chunkData) + loc.indexInChunk;

  EcsCompId compId = 0;
  for (usize i = 0; i != itr->compCount; ++i, ++compId) {
    compId                 = (EcsCompId)bitset_next(itr->mask, compId);
    const usize compIdx    = ecs_archetype_comp_idx(archetype, compId);
    const usize compOffset = compOffsets[compIdx];
    const usize compSize   = compSizes[compIdx];
    itr->comps[i].ptr      = bits_ptr_offset(chunkData, compOffset + compSize * loc.indexInChunk);
    itr->comps[i].size     = compSize;
  }
}

static void ecs_archetype_copy_internal(EcsArchetype* archetype, const u32 dst, const u32 src) {
  const u16* compOffsets = archetype->compOffsetsAndSizes;
  const u16* compSizes   = archetype->compOffsetsAndSizes + archetype->compCount;

  const EcsArchetypeLoc dstLoc = ecs_archetype_location(archetype, dst);
  const EcsArchetypeLoc srcLoc = ecs_archetype_location(archetype, src);

  for (usize compIdx = 0; compIdx != archetype->compCount; ++compIdx) {
    const usize compSize = compSizes[compIdx];

    u8* dstChunkData = bits_ptr_offset(archetype->chunks[dstLoc.chunkIdx], compOffsets[compIdx]);
    u8* srcChunkData = bits_ptr_offset(archetype->chunks[srcLoc.chunkIdx], compOffsets[compIdx]);

    const Mem dstCompMem = mem_create(dstChunkData + compSize * dstLoc.indexInChunk, compSize);
    const Mem srcCompMem = mem_create(srcChunkData + compSize * srcLoc.indexInChunk, compSize);

    mem_cpy(dstCompMem, srcCompMem);
  }
}

EcsArchetype ecs_archetype_create(const EcsDef* def, BitSet mask) {
  diag_assert_msg(bitset_any(mask), "Archetype needs to contain atleast a single component");

  const u32 compCount        = (u32)bitset_count(mask);
  const u32 entitiesPerChunk = ecs_archetype_entities_per_chunk(def, mask);
  diag_assert_msg(entitiesPerChunk, "At least one entity has to fit in an archetype chunk");

  u16* compOffsets = alloc_alloc(g_alloc_heap, sizeof(u16) * compCount * 2, alignof(u16)).ptr;
  u16* compSizes   = compOffsets + compCount;

  usize offset  = sizeof(EcsEntityId) * entitiesPerChunk;
  usize compIdx = 0;
  bitset_for(mask, compId, {
    const usize compSize  = ecs_def_comp_size(def, (EcsCompId)compId);
    const usize compAlign = ecs_def_comp_align(def, (EcsCompId)compId);
    offset                = bits_align(offset, compAlign);
    compOffsets[compIdx]  = (u16)offset;
    compSizes[compIdx]    = (u16)compSize;
    offset += compSize * entitiesPerChunk;
    ++compIdx;
  });

  return (EcsArchetype){
      .mask                = alloc_dup(g_alloc_heap, mask, 1),
      .entitiesPerChunk    = entitiesPerChunk,
      .compOffsetsAndSizes = compOffsets,
      .compCount           = compCount,
      .chunks =
          alloc_alloc(g_alloc_heap, sizeof(void*) * ecs_archetype_max_chunks, alignof(void*)).ptr,
  };
}

void ecs_archetype_destroy(EcsArchetype* archetype) {
  alloc_free(g_alloc_heap, archetype->mask);

  alloc_free(
      g_alloc_heap,
      mem_create(archetype->compOffsetsAndSizes, sizeof(u16) * archetype->compCount * 2));

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
  const u32 entityIdx                             = (u32)(archetype->entityCount++);
  *ecs_archetype_entity_ptr(archetype, entityIdx) = id;
  return entityIdx;
}

EcsEntityId ecs_archetype_remove(EcsArchetype* archetype, const u32 index) {
  const u32 lastIndex = (u32)archetype->entityCount - 1;
  if (index == lastIndex) {
    --archetype->entityCount;
    return 0;
  }

  /**
   * This is not the last entry meaning we get a hole when we remove it.
   * To fix this up we copy the last entity into that hole.
   */

  const EcsEntityId entityToMove = *ecs_archetype_entity_ptr(archetype, lastIndex);
  ecs_archetype_copy_internal(archetype, index, lastIndex);
  --archetype->entityCount;
  return entityToMove;
}

bool ecs_archetype_itr_walk(EcsArchetype* archetype, EcsIterator* itr) {
  diag_assert_msg(bitset_all_of(archetype->mask, itr->mask), "Archetype is missing components");

  if (LIKELY(itr->chunkRemaining)) {
    ++itr->entity;
    --itr->chunkRemaining;
    for (usize i = 0; i != itr->compCount; ++i) {
      itr->comps[i].ptr = bits_ptr_offset(itr->comps[i].ptr, itr->comps[i].size);
    }
    return true;
  }

  // No more entries in the current chunk; jump to the next chunk.
  const usize chunksWithEntities = archetype->entityCount / archetype->entitiesPerChunk;
  if (++itr->chunkIdx >= chunksWithEntities) {
    return false; // Reached the end of the chunks with entities in them.
  }

  const bool isLastChunk = itr->chunkIdx == (chunksWithEntities - 1);
  itr->chunkRemaining    = isLastChunk ? (archetype->entityCount % archetype->entitiesPerChunk)
                                       : archetype->entitiesPerChunk;

  ecs_archetype_itr_init_pointers(archetype, itr, (EcsArchetypeLoc){.chunkIdx = itr->chunkIdx});
  return true;
}

void ecs_archetype_itr_jump(EcsArchetype* archetype, EcsIterator* itr, const u32 index) {
  diag_assert_msg(bitset_all_of(archetype->mask, itr->mask), "Archetype is missing components");

  itr->chunkRemaining = 0;
  ecs_archetype_itr_init_pointers(archetype, itr, ecs_archetype_location(archetype, index));
}

void ecs_archetype_copy_across(
    const BitSet mask, EcsArchetype* dst, const u32 dstIdx, EcsArchetype* src, const u32 srcIdx) {

  const u16* dstCompOffsets = dst->compOffsetsAndSizes;
  const u16* dstCompSizes   = dst->compOffsetsAndSizes + dst->compCount;

  const u16* srcCompOffsets = src->compOffsetsAndSizes;

  const EcsArchetypeLoc dstLoc = ecs_archetype_location(dst, dstIdx);
  const EcsArchetypeLoc srcLoc = ecs_archetype_location(src, srcIdx);

  bitset_for(mask, comp, {
    const usize dstCompIdx = ecs_archetype_comp_idx(dst, (EcsCompId)comp);
    const usize srcCompIdx = ecs_archetype_comp_idx(dst, (EcsCompId)comp);
    const usize compSize   = dstCompSizes[dstCompIdx];

    u8* dstChunkData = bits_ptr_offset(dst->chunks[dstLoc.chunkIdx], dstCompOffsets[dstCompIdx]);
    u8* srcChunkData = bits_ptr_offset(src->chunks[srcLoc.chunkIdx], srcCompOffsets[srcCompIdx]);

    const Mem dstCompMem = mem_create(dstChunkData + compSize * dstLoc.indexInChunk, compSize);
    const Mem srcCompMem = mem_create(srcChunkData + compSize * srcLoc.indexInChunk, compSize);

    mem_cpy(dstCompMem, srcCompMem);
  });
}
