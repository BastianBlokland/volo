#pragma once
#include "core_memory.h"
#include "ecs_entity.h"
#include "ecs_module.h"

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
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
bool ecs_world_entity_exists(const EcsWorld*, EcsEntityId);

/**
 * Schedule an entity to be destroyed at the next flush.
 *
 * Pre-condition: ecs_world_entity_exists(world, entity).
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
void ecs_world_entity_destroy(EcsWorld*, EcsEntityId);

/**
 * Check if an entity has the specified component.
 *
 * Pre-condition: ecs_world_entity_exists(world, entity).
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
#define ecs_world_comp_has_t(_WORLD_, _ENTITY_, _TYPE_)                                            \
  ecs_world_comp_has((_WORLD_), (_ENTITY_), ecs_comp_id(_TYPE_))

bool ecs_world_comp_has(EcsWorld*, EcsEntityId, EcsCompId);

/**
 * Schedule a component to be added at the next flush.
 *
 * Pre-condition: ecs_world_entity_exists(world, entity).
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
#define ecs_world_comp_add_t(_WORLD_, _ENTITY_, _TYPE_, ...)                                       \
  ((_TYPE_*)ecs_world_comp_add(                                                                    \
      (_WORLD_), (_ENTITY_), ecs_comp_id(_TYPE_), mem_struct(_TYPE_, __VA_ARGS__)))

void* ecs_world_comp_add(EcsWorld*, EcsEntityId, EcsCompId, Mem data);

/**
 * Schedule a component to be removed at the next flush.
 *
 * Pre-condition: ecs_world_entity_exists(world, entity)
 * Pre-condition: ecs_world_comp_has(world, entity, comp)
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
#define ecs_world_comp_remove_t(_WORLD_, _ENTITY_, _TYPE_)                                         \
  ecs_world_comp_remove((_WORLD_), (_ENTITY_), ecs_comp_id(_TYPE_))

void ecs_world_comp_remove(EcsWorld*, EcsEntityId, EcsCompId);

/**
 * Flush any queued layout modifications.
 * NOTE: Not valid to be called from inside systems.
 *
 * Pre-condition: !ecs_world_busy()
 */
void ecs_world_flush(EcsWorld*);
