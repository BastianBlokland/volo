#pragma once
#include "core_types.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Identifier for an Entity, unique throughout application lifetime.
 */
typedef u64 EcsEntityId;

/**
 * Structure storing ecs entities and components.
 */
typedef struct sEcsStorage EcsStorage;

/**
 * Create a new EcsStorage structure.
 * Note: Should be destroyed using 'ecs_storage_destroy()'.
 */
EcsStorage* ecs_storage_create(Allocator*);

/**
 * Destroy the given EcsStorage structure.
 */
void ecs_storage_destroy(EcsStorage*);

/**
 * Create a new entity.
 * Pre-condition: g_ecsRunningSystem == true
 */
EcsEntityId ecs_storage_entity_create(EcsStorage*);
