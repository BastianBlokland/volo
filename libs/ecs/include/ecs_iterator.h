#pragma once
#include "core_bitset.h"
#include "ecs_entity.h"

typedef struct {
  void* ptr;
  u32   chunkIdx, chunkRem;
} EcsIteratorArchetype;

typedef struct {
  BitSet               mask;
  usize                compCount;
  EcsIteratorArchetype archetype;
  const EcsEntityId*   entity;
  Mem                  comps[];
} EcsIterator;

#define ecs_iterator_impl_stack(_MASK_)                                                            \
  ecs_iterator_impl_create(                                                                        \
      mem_stack(sizeof(EcsIterator) + bitset_count(_MASK_) * sizeof(Mem)), (_MASK_))

EcsIterator* ecs_iterator_impl_create(Mem itrMem, BitSet mask);
