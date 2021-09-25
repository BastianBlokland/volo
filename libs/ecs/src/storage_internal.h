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

EcsEntityId    ecs_storage_entity_create(EcsStorage*);
bool           ecs_storage_entity_exists(const EcsStorage*, EcsEntityId);
EcsEntityInfo* ecs_storage_entity_info(EcsStorage*, EcsEntityId);
EcsArchetypeId ecs_storage_achetype_find(EcsStorage*, BitSet mask);
EcsArchetypeId ecs_storage_archtype_find_or_create(EcsStorage*, BitSet mask);
void           ecs_storage_flush(EcsStorage*);
