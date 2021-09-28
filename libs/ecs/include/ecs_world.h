#pragma once
#include "core_types.h"
#include "ecs_comp.h"
#include "ecs_entity.h"

// Forward declare from 'ecs_def.h'.
typedef struct sEcsDef EcsDef;

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Ecs world.
 * Containers that stores component-data and provides views for reading / writing that data.
 */
typedef struct sEcsWorld EcsWorld;

/**
 * Create a new (empty) world.
 * NOTE: The given definition can no longer be changed after creating a world from it.
 *
 * Destroy using 'ecs_world_destroy()'.
 */
EcsWorld* ecs_world_create(Allocator*, const EcsDef*);

/**
 * Destroy a Ecs world.
 */
void ecs_world_destroy(EcsWorld*);

/**
 * Retrieve the Ecs definition for the given world.
 */
const EcsDef* ecs_world_def(EcsWorld*);

/**
 * Check if the world is currently busy (being used by a runner for example).
 */
bool ecs_world_busy(const EcsWorld*);

/**
 * Synchonously create a new entity.
 *
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
EcsEntityId ecs_world_entity_create(EcsWorld*);

/**
 * Check if the given entity exists in the world.
 *
 * Pre-condition: EcsEntityId is created with 'ecs_world_entity_create()'
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
bool ecs_world_entity_exists(const EcsWorld*, EcsEntityId);

/**
 * Schedule an entity to be destroyed at the next flush.
 *
 * Pre-condition: ecs_world_entity_exists(world, entity).
 * Pre-condition: EcsEntityId is created with 'ecs_world_entity_create()'
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
void ecs_world_entity_destroy_async(EcsWorld*, EcsEntityId);
