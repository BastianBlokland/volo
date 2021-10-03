#pragma once
#include "ecs_comp.h"
#include "ecs_entity.h"

/**
 * Ecs View.
 * Handle for accessing component data.
 */
typedef struct sEcsView EcsView;

/**
 * Returns how many components this view reads / writes.
 * NOTE: Does not include maybeRead / maybeWrite.
 */
usize ecs_view_comp_count(EcsView*);

/**
 * Check if this view contains the given entity.
 */
bool ecs_view_contains(EcsView*, EcsEntityId);

/**
 * Get a read-only pointer to a component.
 *
 * Pre-condition: ecs_view_contains(view, entity)
 * Pre-condition: ecs_world_comp_has(entity, comp)
 * Pre-condition: view has 'Read' access to the given component type.
 */
#define ecs_view_comp_read_t(_VIEW_, _ENTITY_, _TYPE_)                                             \
  ((const _TYPE_*)ecs_view_comp_read((_VIEW_), (_ENTITY_), ecs_comp_id(_TYPE_)))

const void* ecs_view_comp_read(EcsView*, EcsEntityId, EcsCompId);

/**
 * Get a read-write pointer to a component.
 *
 * Pre-condition: ecs_view_contains(view, entity)
 * Pre-condition: ecs_world_comp_has(entity, comp)
 * Pre-condition: view has 'Write' access to the given component type.
 */
#define ecs_view_comp_write_t(_VIEW_, _ENTITY_, _TYPE_)                                            \
  ((_TYPE_*)ecs_view_comp_write((_VIEW_), (_ENTITY_), ecs_comp_id(_TYPE_)))

void* ecs_view_comp_write(EcsView*, EcsEntityId, EcsCompId);
