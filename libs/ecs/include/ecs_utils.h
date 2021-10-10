#pragma once
#include "ecs_world.h"

/**
 * Get a read-only pointer to an existing component or add a new component if the entity does not
 * have the requested component.
 *
 * Pre-condition: iterator has been intialized using ecs_view_itr_walk() / ecs_view_itr_jump().
 * Pre-condition: view has 'Read' access to the given component type.
 */
#define ecs_utils_read_or_add_t(_WORLD_, _ITR_, _TYPE_)                                            \
  ((const _TYPE_*)ecs_utils_read_or_add((_WORLD_), (_ITR_), ecs_comp_id(_TYPE_)))

const void* ecs_utils_read_or_add(EcsWorld*, const EcsIterator*, EcsCompId);

/**
 * Get a read/write pointer to an existing component or add a new component if the entity does not
 * have the requested component.
 *
 * Pre-condition: iterator has been intialized using ecs_view_itr_walk() / ecs_view_itr_jump().
 * Pre-condition: view has 'Write' access to the given component type.
 */
#define ecs_utils_write_or_add_t(_WORLD_, _ITR_, _TYPE_)                                           \
  ((_TYPE_*)ecs_utils_write_or_add((_WORLD_), (_ITR_), ecs_comp_id(_TYPE_)))

void* ecs_utils_write_or_add(EcsWorld*, const EcsIterator*, EcsCompId);
