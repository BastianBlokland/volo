#pragma once
#include "core/bitset.h"
#include "ecs/comp.h"
#include "ecs/forward.h"

#include "comp.h"

#define ecs_iterator_size_max 64

typedef struct sEcsIterator EcsIterator;

struct sEcsIterator {
  u16                compCount;
  u16                archetypeIdx;
  u16                chunksToSkip;         // Skip this amount of chunks, used for stepped iter.
  u16                chunksLimitRemaining; // Max chunks to process, used for stepped iter.
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

MAYBE_UNUSED INLINE_HINT static Mem ecs_iterator_access(const EcsIterator* itr, const EcsCompId i) {
  return itr->comps[ecs_comp_index(itr->mask, i)];
}
