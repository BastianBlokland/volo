#pragma once
#include "core_dynarray.h"
#include "core_thread.h"
#include "ecs_def.h"

#include "entity_allocator_internal.h"

typedef u32 EcsArchetypeId;

typedef struct {
  const EcsDef* def;

  EntityAllocator entityAllocator;
  DynArray        entities; // EcsEntityInfo[].

  ThreadSpinLock newEntitiesLock;
  DynArray       newEntities; // EcsEntityId[].

  DynArray archetypes; // EcsArchetype[].
} EcsStorage;

EcsStorage ecs_storage_create(Allocator*, const EcsDef*);
void       ecs_storage_destroy(EcsStorage*);

EcsEntityId    ecs_storage_entity_create(EcsStorage*);
bool           ecs_storage_entity_exists(const EcsStorage*, EcsEntityId);
BitSet         ecs_storage_entity_mask(EcsStorage*, EcsEntityId);
void*          ecs_storage_entity_comp(EcsStorage*, EcsEntityId, EcsCompId);
void           ecs_storage_entity_move(EcsStorage*, EcsEntityId, EcsArchetypeId newArchetypeId);
void           ecs_storage_entity_destroy(EcsStorage*, EcsEntityId);
EcsArchetypeId ecs_storage_archetype_find(EcsStorage*, BitSet mask);
EcsArchetypeId ecs_storage_archetype_create(EcsStorage*, BitSet mask);

/**
 * Flush any entities that where created since the last call.
 */
void ecs_storage_flush_new_entities(EcsStorage*);
