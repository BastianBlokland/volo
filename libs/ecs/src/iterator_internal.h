#pragma once
#include "core_bitset.h"
#include "ecs_comp.h"
#include "ecs_entity.h"

#include "comp_internal.h"

#define ecs_iterator_size_max 64

typedef struct sEcsIterator EcsIterator;

struct sEcsIterator {
  u16                compCount;
  u16                archetypeIdx;
  u16                chunksToSkip;         // Skip this amount of chunks, used for partial iter.
  u16                chunksLimitRemaining; // Max chunks to process, used for partial iter.
  u32                chunkIdx, chunkRemaining;
  BitSet             mask;
  void*              context;
  const EcsEntityId* entity;
  Mem                comps[];
};

ASSERT(sizeof(EcsIterator) < ecs_iterator_size_max, "EcsIterator size exceeds the maximum");

#define ecs_iterator_stack(_MASK_)                                                                 \
  ecs_iterator_create(                                                                             \
      mem_stack(sizeof(EcsIterator) + ecs_comp_mask_count(_MASK_) * sizeof(Mem)), (_MASK_))

EcsIterator* ecs_iterator_create(Mem mem, BitSet mask);
EcsIterator* ecs_iterator_create_with_count(Mem mem, BitSet mask, u16 compCount);
void         ecs_iterator_reset(EcsIterator*);

INLINE_HINT static Mem ecs_iterator_access(const EcsIterator* itr, const EcsCompId id) {
  return itr->comps[ecs_comp_index(itr->mask, id)];
}
