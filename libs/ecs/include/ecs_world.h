#pragma once
#include "core_memory.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "ecs_view.h"

// Forward declare from 'ecs_def.h'.
typedef struct sEcsDef EcsDef;

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

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
 *
 * Pre-condition: !ecs_world_busy()
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
 * Retrieve the global entity (an entity that always exists and cannot be destroyed).
 */
EcsEntityId ecs_world_global(const EcsWorld*);

/**
 * Retrieve a view for accessing component data.
 * NOTE: In an Ecs System this is only valid if your system has declared access to the view.
 * NOTE: View pointers should not be stored.
 *
 * Pre-condition: !ecs_world_busy() ||
 *                (g_ecsRunningSystem && ecs_def_system_has_access(g_ecsRunningSystemId, view)
 */
#define ecs_world_view_t(_WORLD_, _VIEW_) ecs_world_view((_WORLD_), ecs_view_id(_VIEW_))

EcsView* ecs_world_view(EcsWorld*, EcsViewId);

/**
 * Synchonously create a new entity.
 *
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
EcsEntityId ecs_world_entity_create(EcsWorld*);

/**
 * Schedule an entity to be destroyed at the next flush.
 *
 * Pre-condition: ecs_world_exists(world, entity)
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
void ecs_world_entity_destroy(EcsWorld*, EcsEntityId);

/**
 * Check if the given entity exists in the world.
 *
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
bool ecs_world_exists(const EcsWorld*, EcsEntityId);

/**
 * Check if an entity has the specified component.
 *
 * Pre-condition: ecs_world_exists(world, entity)
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
#define ecs_world_has_t(_WORLD_, _ENTITY_, _TYPE_)                                                 \
  ecs_world_has((_WORLD_), (_ENTITY_), ecs_comp_id(_TYPE_))

bool ecs_world_has(EcsWorld*, EcsEntityId, EcsCompId);

/**
 * Schedule a component to be added at the next flush.
 * NOTE: The returned pointer is valid until the next flush.
 * NOTE: Non-empty components without a combinator can only be added if the entity doesn't have it.
 *
 * Pre-condition: ecs_world_exists(world, entity)
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
#define ecs_world_add_t(_WORLD_, _ENTITY_, _TYPE_, ...)                                            \
  ((_TYPE_*)ecs_world_add(                                                                         \
      (_WORLD_), (_ENTITY_), ecs_comp_id(_TYPE_), mem_struct(_TYPE_, __VA_ARGS__)))

#define ecs_world_add_empty_t(_WORLD_, _ENTITY_, _TYPE_)                                           \
  ((void)ecs_world_add((_WORLD_), (_ENTITY_), ecs_comp_id(_TYPE_), mem_empty))

void* ecs_world_add(EcsWorld*, EcsEntityId, EcsCompId, Mem data);

/**
 * Schedule a component to be removed at the next flush.
 *
 * Pre-condition: ecs_world_exists(world, entity)
 * Pre-condition: ecs_world_has(world, entity, comp)
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
#define ecs_world_remove_t(_WORLD_, _ENTITY_, _TYPE_)                                              \
  ecs_world_remove((_WORLD_), (_ENTITY_), ecs_comp_id(_TYPE_))

void ecs_world_remove(EcsWorld*, EcsEntityId, EcsCompId);

/**
 * Flush any queued layout modifications.
 * NOTE: Not valid to be called from inside systems.
 *
 * Pre-condition: !ecs_world_busy()
 */
void ecs_world_flush(EcsWorld*);

typedef struct {
  u32 entityCount; // Amount of entities that exist in the world.
  u32 archetypeCount;
} EcsWorldStats;

/**
 * Query statistics for the given world.
 */
EcsWorldStats ecs_world_stats_query(const EcsWorld*);
