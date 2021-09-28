#pragma once
#include "core_dynarray.h"
#include "core_thread.h"

#include "archetype_internal.h"
#include "entity_allocator_internal.h"

typedef u32 EcsArchetypeId;

typedef struct {
  u32            serial;
  EcsArchetypeId archetype;
  u32            archetypeIndex;
} EcsEntityInfo;

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

/**
 * Register a new entity.
 * NOTE: Created entities may not have a 'EcsEntityInfo' until 'ecs_storage_flush_new_entities()' is
 * called (meaning 'ecs_storage_entity_info()' will return null). 'ecs_storage_entity_exists()' will
 * however immediately recognize the entities.
 */
EcsEntityId ecs_storage_entity_create(EcsStorage*);

bool           ecs_storage_entity_exists(const EcsStorage*, EcsEntityId);
EcsEntityInfo* ecs_storage_entity_info(EcsStorage*, EcsEntityId);
void           ecs_storage_entity_destroy(EcsStorage*, EcsEntityId);
EcsArchetypeId ecs_storage_achetype_find(EcsStorage*, BitSet mask);
EcsArchetypeId ecs_storage_archtype_find_or_create(EcsStorage*, BitSet mask);

/**
 * Flush any entities that where created since the last call.
 */
void ecs_storage_flush_new_entities(EcsStorage*);
