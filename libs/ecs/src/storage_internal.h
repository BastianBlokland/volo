#pragma once
#include "core_dynarray.h"
#include "core_thread.h"
#include "ecs_def.h"

#include "entity_allocator_internal.h"
#include "iterator_internal.h"

typedef u32 EcsArchetypeId;

typedef struct {
  const EcsDef* def;

  EntityAllocator entityAllocator;
  DynArray        entities; // EcsEntityInfo[].

  ThreadSpinLock newEntitiesLock;
  DynArray       newEntities; // EcsEntityId[].

  DynArray archetypes; // EcsArchetype[].
} EcsStorage;

i8 ecs_compare_archetype(const void* a, const void* b);

EcsStorage ecs_storage_create(Allocator*, const EcsDef*);
void       ecs_storage_destroy(EcsStorage*);

EcsEntityId    ecs_storage_entity_create(EcsStorage*);
bool           ecs_storage_entity_exists(const EcsStorage*, EcsEntityId);
BitSet         ecs_storage_entity_mask(EcsStorage*, EcsEntityId);
EcsArchetypeId ecs_storage_entity_archetype(EcsStorage*, EcsEntityId);
void           ecs_storage_entity_move(EcsStorage*, EcsEntityId, EcsArchetypeId newArchetypeId);
void           ecs_storage_entity_destroy(EcsStorage*, EcsEntityId);

usize          ecs_storage_archetype_entities_per_chunk(EcsStorage*, EcsArchetypeId);
EcsArchetypeId ecs_storage_archetype_find(EcsStorage*, BitSet mask);
EcsArchetypeId ecs_storage_archetype_create(EcsStorage*, BitSet mask);

bool ecs_storage_itr_walk(EcsStorage*, EcsIterator*, EcsArchetypeId);
void ecs_storage_itr_jump(EcsStorage*, EcsIterator*, EcsEntityId);

/**
 * Flush any entities that where created since the last call.
 */
void ecs_storage_flush_new_entities(EcsStorage*);
