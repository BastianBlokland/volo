#include "core_dynarray.h"
#include "core_thread.h"
#include "ecs_storage.h"

#include "archetype_internal.h"
#include "entity_allocator_internal.h"

typedef struct {
  u32            serial;
  EcsArchetypeId archetype;
  u32            archetypeIndex;
} EcsEntityInfo;

struct sEcsStorage {
  EcsMeta* meta;

  EntityAllocator entityAllocator;
  DynArray        entities; // EcsEntityInfo[].

  ThreadSpinLock newEntitiesLock;
  DynArray       newEntities; // EcsEntityId[].

  DynArray archetypes; // EcsArchetype[].

  Allocator* memoryAllocator;
};

/**
 * Internal api for accessing entity information.
 * Returns null if the entity cannot be found.
 */
EcsEntityInfo* ecs_storage_entity_info(EcsStorage*, EcsEntityId);

/**
 * Find the archetype with the given component mask.
 * Returns sentinel_u32 if no archtype could be found with the given mask.
 */
EcsArchetypeId ecs_storage_achetype_find(EcsStorage*, BitSet mask);

/**
 * Find the archetype with the given component mask.
 * Creates a new archetype if none could be found.
 */
EcsArchetypeId ecs_storage_archtype_find_or_create(EcsStorage*, BitSet mask);
