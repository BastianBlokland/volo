#include "core_dynarray.h"
#include "core_thread.h"
#include "ecs_storage.h"

#include "entity_allocator_internal.h"

typedef struct {
  u32            serial;
  EcsArchetypeId archetype;
  u32            archetypeIndex;
} EcsEntityInfo;

struct sEcsStorage {
  EntityAllocator entityAllocator;
  DynArray        entities; // EcsEntityInfo[].

  ThreadSpinLock newEntitiesLock;
  DynArray       newEntities; // EcsEntityId[].

  Allocator* memoryAllocator;
};

/**
 * Internal api for accessing entity information.
 * Returns null if the entity cannot be found.
 */
EcsEntityInfo* ecs_storage_entity_info(EcsStorage*, EcsEntityId);
