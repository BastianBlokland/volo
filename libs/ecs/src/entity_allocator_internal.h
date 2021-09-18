#pragma once
#include "core_dynbitset.h"
#include "core_thread.h"
#include "ecs_storage.h"

/**
 * Entity index, assigned in a first-free manor. Unique within all alive entities.
 */
#define ecs_entity_id_index(_ENTITY_ID_) ((u32)((_ENTITY_ID_) >> 0))

/**
 * Unique identifier of the entity, assigned linearly starting from 1.
 * Not meant to wrap around as it breaks the invariant that EntityIds are never reused, if a bigger
 * serial counter is needed then more bits can be assigned (the index part most likely doesn't need
 * 32 bits).
 */
#define ecs_entity_id_serial(_ENTITY_ID_) ((u32)((_ENTITY_ID_) >> 32))

typedef struct {
  ThreadSpinLock lock;
  u32            serialCounter;
  DynBitSet      freeIndices;
  usize          totalIndices;
} EntityAllocator;

EntityAllocator entity_allocator_create(Allocator*);
void            entity_allocator_destroy(EntityAllocator*);

/**
 * Acquire a new entity-id.
 * Note: Thread-safe.
 * Should be freed with: 'entity_allocator_free()'.
 */
EcsEntityId entity_allocator_alloc(EntityAllocator*);

/**
 * Release an entity-id.
 * Note: Thread-safe.
 */
void entity_allocator_free(EntityAllocator*, EcsEntityId);
