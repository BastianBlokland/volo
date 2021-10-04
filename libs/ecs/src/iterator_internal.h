#pragma once
#include "core_bitset.h"
#include "ecs_comp.h"
#include "ecs_entity.h"

typedef struct {
  BitSet             mask;
  usize              compCount;
  u32                chunkIdx, chunkRemaining;
  const EcsEntityId* entity;
  Mem                comps[];
} EcsIterator;

#define ecs_iterator_stack(_MASK_)                                                                 \
  ecs_iterator_create(mem_stack(sizeof(EcsIterator) + bitset_count(_MASK_) * sizeof(Mem)), (_MASK_))

EcsIterator* ecs_iterator_create(Mem mem, BitSet mask);
void         ecs_iterator_reset(EcsIterator*);
Mem          ecs_iterator_access(EcsIterator*, EcsCompId);
