#pragma once
#include "core_memory.h"
#include "ecs_comp.h"
#include "ecs_entity.h"

typedef struct sEcsView     EcsView;
typedef struct sEcsIterator EcsIterator;

/**
 * Returns how many components this view reads / writes.
 */
usize ecs_view_comp_count(EcsView*);

/**
 * Check if this view contains the given entity.
 */
bool ecs_view_contains(EcsView*, EcsEntityId);

/**
 * Create a new iterator for the given view.
 * NOTE: Allocates memory in the function scope, meaning iterators should not be created in a loop.
 * NOTE: _VIEW_ is expanded twice, so care must be taken when providing a complex expression.
 */
#define ecs_view_itr_stack(_VIEW_)                                                                 \
  ecs_view_itr_create(mem_stack(64 + sizeof(Mem) * ecs_view_comp_count(_VIEW_)), (_VIEW_))

EcsIterator* ecs_view_itr_create(Mem, EcsView*);
void         ecs_view_itr_reset(EcsIterator*);

/**
 * Advance the iterator to the next entity in the view.
 */
bool ecs_view_itr_walk(EcsIterator*);

/**
 * Jump to a specific entity in the view.
 *
 * Pre-condition: ecs_view_contains(view, entity)
 */
void ecs_view_itr_jump(EcsIterator*, EcsEntityId);

/**
 * Get the current entity for the given iterator.
 *
 * Pre-condition: iterator has been intialized using ecs_view_itr_walk() / ecs_view_itr_jump().
 */
EcsEntityId ecs_view_entity(const EcsIterator*);

/**
 * Get a read-only pointer to a component.
 *
 * Pre-condition: iterator has been intialized using ecs_view_itr_walk() / ecs_view_itr_jump().
 * Pre-condition: view has 'Read' access to the given component type.
 */
#define ecs_view_read_t(_ITR_, _TYPE_) ((const _TYPE_*)ecs_view_read((_ITR_), ecs_comp_id(_TYPE_)))

const void* ecs_view_read(const EcsIterator*, EcsCompId);

/**
 * Get a read-write pointer to a component.
 *
 * Pre-condition: iterator has been intialized using ecs_view_walk() / ecs_view_jump().
 * Pre-condition: view has 'Write' access to the given component type.
 */
#define ecs_view_write_t(_ITR_, _TYPE_) ((_TYPE_*)ecs_view_write((_ITR_), ecs_comp_id(_TYPE_)))

void* ecs_view_write(const EcsIterator*, EcsCompId);
