#pragma once
#include "ecs_world.h"

/**
 * Get a read-only pointer to an existing component or add a new component if the entity does not
 * have the requested component.
 *
 * Pre-condition: iterator has been intialized using ecs_view_walk() / ecs_view_jump().
 * Pre-condition: view has 'Read' access to the given component type.
 */
#define ecs_utils_read_or_add_t(_WORLD_, _ITR_, _TYPE_)                                            \
  ((const _TYPE_*)ecs_utils_read_or_add((_WORLD_), (_ITR_), ecs_comp_id(_TYPE_)))

const void* ecs_utils_read_or_add(EcsWorld*, const EcsIterator*, EcsCompId);

/**
 * Get a read/write pointer to an existing component or add a new component if the entity does not
 * have the requested component.
 *
 * Pre-condition: iterator has been intialized using ecs_view_walk() / ecs_view_jump().
 * Pre-condition: view has 'Write' access to the given component type.
 */
#define ecs_utils_write_or_add_t(_WORLD_, _ITR_, _TYPE_)                                           \
  ((_TYPE_*)ecs_utils_write_or_add((_WORLD_), (_ITR_), ecs_comp_id(_TYPE_)))

void* ecs_utils_write_or_add(EcsWorld*, const EcsIterator*, EcsCompId);

/**
 * Add the component if the entity does not have the component yet.
 * Returns a pointer to the added component or null if the component already existed.
 *
 * Pre-condition: ecs_world_exists(world, entity)
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
#define ecs_utils_maybe_add_t(_WORLD_, _ENTITY_, _TYPE_)                                           \
  ((_TYPE_*)ecs_utils_maybe_add((_WORLD_), (_ENTITY_), ecs_comp_id(_TYPE_)))

void* ecs_utils_maybe_add(EcsWorld*, EcsEntityId, EcsCompId);

/**
 * Remove the component if the entity has the specified component.
 * Returns true if the component was removed, otherwise false.
 *
 * Pre-condition: ecs_world_exists(world, entity)
 * Pre-condition: !ecs_world_busy() || g_ecsRunningSystem
 */
#define ecs_utils_maybe_remove_t(_WORLD_, _ENTITY_, _TYPE_)                                        \
  ecs_utils_maybe_remove((_WORLD_), (_ENTITY_), ecs_comp_id(_TYPE_))

bool ecs_utils_maybe_remove(EcsWorld*, EcsEntityId, EcsCompId);
