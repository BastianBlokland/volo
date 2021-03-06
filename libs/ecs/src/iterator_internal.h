#pragma once
#include "core_bitset.h"
#include "ecs_comp.h"
#include "ecs_entity.h"

#include "comp_internal.h"

#define ecs_iterator_size_max 64

typedef struct sEcsIterator EcsIterator;

struct sEcsIterator {
  BitSet             mask;
  usize              compCount;
  void*              context;
  u32                archetypeIdx;
  u32                chunkIdx, chunkRemaining;
  const EcsEntityId* entity;
  Mem                comps[];
};

ASSERT(sizeof(EcsIterator) < ecs_iterator_size_max, "EcsIterator size exceeds the maximum");

#define ecs_iterator_stack(_MASK_)                                                                 \
  ecs_iterator_create(                                                                             \
      mem_stack(sizeof(EcsIterator) + ecs_comp_mask_count(_MASK_) * sizeof(Mem)), (_MASK_))

EcsIterator* ecs_iterator_create(Mem mem, BitSet mask);
EcsIterator* ecs_iterator_create_with_count(Mem mem, BitSet mask, usize compCount);
void         ecs_iterator_reset(EcsIterator*);

INLINE_HINT static Mem ecs_iterator_access(const EcsIterator* itr, const EcsCompId id) {
  return itr->comps[ecs_comp_index(itr->mask, id)];
}
