#pragma once
#include "core_dynbitset.h"
#include "core_thread.h"

#include "entity_internal.h"

typedef struct {
  ThreadSpinLock lock;
  u64            serialCounter;
  DynBitSet      freeIndices;
  usize          totalIndices;
} EntityAllocator;

EntityAllocator entity_allocator_create(Allocator*);
void            entity_allocator_destroy(EntityAllocator*);

/**
 * Acquire a new entity-id.
 * Should be freed with: 'entity_allocator_free()'.
 */
EcsEntityId entity_allocator_alloc(EntityAllocator*);

/**
 * Release an entity-id.
 */
void entity_allocator_free(EntityAllocator*, EcsEntityId);

/**
 * Count the amount of active (allocated and not yet freed) entities.
 * NOTE: Relatively expensive as it has to walk the entire freeIndices bitset.
 */
u32 entity_allocator_count_active(const EntityAllocator*);
