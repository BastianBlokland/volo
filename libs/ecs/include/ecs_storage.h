#pragma once
#include "core_types.h"
#include "ecs_def.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Identifier for an Entity, unique throughout application lifetime.
 */
typedef u64 EcsEntityId;

/**
 * Identifier for an Archetype.
 */
typedef u32 EcsArchetypeId;

/**
 * Structure storing ecs entities and components.
 */
typedef struct sEcsStorage EcsStorage;

/**
 * Create a new EcsStorage structure.
 * Note: Should be destroyed using 'ecs_storage_destroy()'.
 */
EcsStorage* ecs_storage_create(Allocator*, EcsDef*);

/**
 * Destroy the given EcsStorage structure.
 */
void ecs_storage_destroy(EcsStorage*);

/**
 * Create a new entity.
 * Pre-condition: g_ecsRunningSystem == true
 */
EcsEntityId ecs_storage_entity_create(EcsStorage*);

/**
 * Check if the given entity exists in the storage.
 */
bool ecs_storage_entity_exists(EcsStorage*, EcsEntityId);
