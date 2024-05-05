#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_time.h"
#include "ecs_runner.h"
#include "log_logger.h"
#include "trace_tracer.h"

#include "buffer_internal.h"
#include "def_internal.h"
#include "finalizer_internal.h"
#include "storage_internal.h"
#include "view_internal.h"
#include "world_internal.h"

// #define VOLO_ECS_WORLD_LOGGING_VERBOSE

typedef enum {
  EcsWorldFlags_None,
  EcsWorldFlags_Busy = 1 << 0, // For example set when a runner is active on this world.
} EcsWorldFlags;

struct sEcsWorld {
  const EcsDef* def;
  EcsFinalizer  finalizer;
  EcsStorage    storage;
  DynArray      views; // EcsView[].

  ThreadSpinLock bufferLock;
  EcsBuffer      buffer;

  EcsWorldFlags flags;
  EcsEntityId   globalEntity;
  Allocator*    alloc;

  TimeDuration lastFlushDur;
  u32          lastFlushEntities;
};

static usize
ecs_world_archetype_track(EcsWorld* world, const EcsArchetypeId id, const BitSet mask) {
  usize trackingViews = 0;
  dynarray_for_t(&world->views, EcsView, view) {
    if (ecs_view_maybe_track(view, id, mask)) {
      ++trackingViews;
    }
  }
  return trackingViews;
}

static EcsArchetypeId ecs_world_archetype_find_or_create(EcsWorld* world, const BitSet mask) {
  if (!bitset_any(mask)) {
    return sentinel_u32;
  }
  const EcsArchetypeId existingId = ecs_storage_archetype_find(&world->storage, mask);
  if (!sentinel_check(existingId)) {
    return existingId;
  }
  const EcsArchetypeId newId         = ecs_storage_archetype_create(&world->storage, mask);
  const usize          trackingViews = ecs_world_archetype_track(world, newId, mask);

#if defined(VOLO_ECS_WORLD_LOGGING_VERBOSE)
  log_d(
      "Ecs archetype created",
      log_param("components", fmt_int(ecs_comp_mask_count(mask))),
      log_param(
          "entities-per-chunk",
          fmt_int(ecs_storage_archetype_entities_per_chunk(&world->storage, newId))),
      log_param("tracking-views", fmt_int(trackingViews)));
#endif

  (void)trackingViews;
  return newId;
}

static void ecs_world_apply_added_comps(
    EcsStorage* storage, EcsBuffer* buffer, const usize idx, const BitSet currentMask) {

  const EcsEntityId entity     = ecs_buffer_entity(buffer, idx);
  const BitSet      addedComps = ecs_buffer_entity_added(buffer, idx);
  if (!bitset_any(addedComps)) {
    return;
  }

  /**
   * NOTE: 'addedComps' can contain empty components which are not present in the
   * 'ecs_buffer_comp_begin' / 'ecs_buffer_comp_next' iteration.
   */

  BitSet initializedComps = ecs_comp_mask_stack(storage->def);
  mem_set(initializedComps, 0);
  mem_cpy(initializedComps, currentMask);

  EcsIterator* storageItr = ecs_iterator_stack(addedComps);
  ecs_storage_itr_jump(storage, storageItr, entity);

  for (EcsBufferCompData* bufferItr = ecs_buffer_comp_begin(buffer, idx); bufferItr;
       bufferItr                    = ecs_buffer_comp_next(bufferItr)) {

    const EcsCompId compId   = ecs_buffer_comp_id(bufferItr);
    const Mem       compData = ecs_buffer_comp_data(buffer, bufferItr);

    if (!ecs_comp_has(initializedComps, compId)) {
      mem_cpy(ecs_iterator_access(storageItr, compId), compData);
      bitset_set(initializedComps, compId);
      continue;
    }

    EcsCompCombinator combinator = ecs_def_comp_combinator(storage->def, compId);
    if (combinator) {
      combinator(ecs_iterator_access(storageItr, compId).ptr, compData.ptr);
      continue;
    }

    diag_assert_fail(
        "Duplicate addition of {} to entity {}",
        fmt_text(ecs_def_comp_name(buffer->def, compId)),
        fmt_int(entity, .base = 16));
  }
}

static void ecs_world_queue_finalize_added(EcsWorld* world, EcsBuffer* buffer, const usize idx) {
  for (EcsBufferCompData* bufferItr = ecs_buffer_comp_begin(buffer, idx); bufferItr;
       bufferItr                    = ecs_buffer_comp_next(bufferItr)) {

    const EcsCompId compId   = ecs_buffer_comp_id(bufferItr);
    const Mem       compData = ecs_buffer_comp_data(buffer, bufferItr);
    ecs_finalizer_push(&world->finalizer, compId, compData.ptr);
  }
}

/**
 * Compute a mask with the removed components for the given entry in the buffer.
 *
 * This is not the same as 'ecs_buffer_entity_removed()' as component addition takes precedence
 * over removal and the buffer could contain both for the same component.
 */
static void ecs_world_removed_comps_mask(EcsBuffer* buffer, const usize idx, BitSet out) {
  bitset_clear_all(out);
  bitset_or(out, ecs_buffer_entity_removed(buffer, idx));
  bitset_xor(out, ecs_buffer_entity_added(buffer, idx));
  bitset_and(out, ecs_buffer_entity_removed(buffer, idx));
}

/**
 * Compute the new component mask for the given entry in the buffer.
 */
static void
ecs_world_new_comps_mask(EcsBuffer* buffer, const usize idx, const BitSet currentMask, BitSet out) {
  bitset_clear_all(out);
  bitset_or(out, currentMask);
  bitset_xor(out, ecs_buffer_entity_removed(buffer, idx));
  bitset_or(out, ecs_buffer_entity_added(buffer, idx));
}

static void ecs_world_validate_sys_parallel(const EcsWorld* world, const EcsSystemDef* sysDef) {
  MAYBE_UNUSED u32 writeViews = 0;
  dynarray_for_t(&sysDef->viewIds, EcsViewId, viewId) {
    diag_assert_msg(*viewId < world->views.size, "Invalid view-id: {}", fmt_int(*viewId));
    const EcsView* view               = dynarray_at_t(&world->views, *viewId, EcsView);
    const bool     anyWrite           = bitset_any(ecs_view_mask(view, EcsViewMask_AccessWrite));
    const bool     allowParallelWrite = (view->flags & EcsViewFlags_AllowParallelRandomWrite) != 0;
    if (anyWrite && !allowParallelWrite) {
      ++writeViews;
    }
  }
  diag_assert_msg(
      writeViews <= 1,
      "Parallel system '{}' has multiple ({}) write-views, this is potentially unsafe",
      fmt_text(sysDef->name),
      fmt_int(writeViews));
}

static void ecs_world_validate(const EcsWorld* world) {
  dynarray_for_t(&world->def->systems, EcsSystemDef, sysDef) {
    if (sysDef->parallelCount > 1) {
      ecs_world_validate_sys_parallel(world, sysDef);
    }
  }
}

EcsWorld* ecs_world_create(Allocator* alloc, const EcsDef* def) {
  ecs_def_freeze((EcsDef*)def);

  EcsWorld* world = alloc_alloc_t(alloc, EcsWorld);
  *world          = (EcsWorld){
               .def       = def,
               .finalizer = ecs_finalizer_create(alloc, def),
               .storage   = ecs_storage_create(alloc, def),
               .views     = dynarray_create_t(alloc, EcsView, ecs_def_view_count(def)),
               .buffer    = ecs_buffer_create(alloc, def),
               .alloc     = alloc,
  };
  world->globalEntity = ecs_storage_entity_create(&world->storage);

  dynarray_for_t(&def->views, EcsViewDef, viewDef) {
    *dynarray_push_t(&world->views, EcsView) =
        ecs_view_create(alloc, &world->storage, def, viewDef);
  }

  ecs_world_validate(world);

  log_d(
      "Ecs world created",
      log_param("modules", fmt_int(def->modules.size)),
      log_param("components", fmt_int(def->components.size)),
      log_param("systems", fmt_int(def->systems.size)),
      log_param("views", fmt_int(def->views.size)));

  return world;
}

void ecs_world_destroy(EcsWorld* world) {
  diag_assert(!ecs_world_busy(world));

  ecs_def_unfreeze((EcsDef*)world->def);

  // Finalize (invoke destructors) all components on all entities.
  ecs_storage_queue_finalize_all(&world->storage, &world->finalizer);
  ecs_buffer_queue_finalize_all(&world->buffer, &world->finalizer);

  ecs_finalizer_flush(&world->finalizer);
  ecs_finalizer_destroy(&world->finalizer);

  ecs_storage_destroy(&world->storage);
  ecs_buffer_destroy(&world->buffer);

  dynarray_for_t(&world->views, EcsView, view) { ecs_view_destroy(world->alloc, world->def, view); }
  dynarray_destroy(&world->views);

  log_d("Ecs world destroyed");

  alloc_free_t(world->alloc, world);
}

const EcsDef* ecs_world_def(EcsWorld* world) { return world->def; }

bool ecs_world_busy(const EcsWorld* world) { return (world->flags & EcsWorldFlags_Busy) != 0; }

EcsEntityId ecs_world_global(const EcsWorld* world) { return world->globalEntity; }

EcsView* ecs_world_view(EcsWorld* world, const EcsViewId view) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(
      !g_ecsRunningSystem || ecs_def_system_has_access(world->def, g_ecsRunningSystemId, view),
      "System {} has not declared access to view {}",
      fmt_text(ecs_def_system_name(world->def, g_ecsRunningSystemId)),
      fmt_text(ecs_def_view_name(world->def, view)));
  diag_assert_msg(view < world->views.size, "Invalid view id");

  return &dynarray_begin_t(&world->views, EcsView)[view];
}

EcsEntityId ecs_world_entity_create(EcsWorld* world) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  return ecs_storage_entity_create(&world->storage);
}

void ecs_world_entity_destroy(EcsWorld* world, const EcsEntityId entity) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity, .base = 16));
  diag_assert_msg(entity != world->globalEntity, "The global entity cannot be destroyed");

  diag_assert_msg(
      ecs_world_exists(world, entity),
      "Unable to enqueue destruction of entity {}, reason: entity does not exist",
      fmt_int(entity, .base = 16));

  thread_spinlock_lock(&world->bufferLock);
  ecs_buffer_destroy_entity(&world->buffer, entity);
  thread_spinlock_unlock(&world->bufferLock);
}

bool ecs_world_exists(const EcsWorld* world, const EcsEntityId entity) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity, .base = 16));

  return ecs_storage_entity_exists(&world->storage, entity);
}

bool ecs_world_has(const EcsWorld* world, const EcsEntityId entity, const EcsCompId comp) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity, .base = 16));

  diag_assert_msg(
      ecs_storage_entity_exists(&world->storage, entity),
      "Unable to check for {} on entity {}, reason: entity does not exist",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity, .base = 16));

  const BitSet entityMask = ecs_storage_entity_mask(&world->storage, entity);
  return entityMask.size ? ecs_comp_has(entityMask, comp) : false;
}

void* ecs_world_add(
    EcsWorld* world, const EcsEntityId entity, const EcsCompId comp, const Mem data) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity, .base = 16));

  diag_assert_msg(
      ecs_storage_entity_exists(&world->storage, entity),
      "Unable to add {} to entity {}, reason: entity does not exist",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity, .base = 16));

  thread_spinlock_lock(&world->bufferLock);
  void* result = ecs_buffer_comp_add(&world->buffer, entity, comp, data);
  thread_spinlock_unlock(&world->bufferLock);
  return result;
}

void ecs_world_remove(EcsWorld* world, const EcsEntityId entity, const EcsCompId comp) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity, .base = 16));

  diag_assert_msg(
      ecs_storage_entity_exists(&world->storage, entity),
      "Unable to remove {} from entity {}, reason: entity does not exist",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity, .base = 16));

  diag_assert_msg(
      ecs_world_has(world, entity, comp),
      "Unable to remove {} from entity {}, reason: entity does not have the specified component",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity, .base = 16));

  thread_spinlock_lock(&world->bufferLock);
  ecs_buffer_comp_remove(&world->buffer, entity, comp);
  thread_spinlock_unlock(&world->bufferLock);
}

EcsArchetypeId ecs_world_entity_archetype(const EcsWorld* world, const EcsEntityId entity) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity, .base = 16));

  return ecs_storage_entity_archetype(&world->storage, entity);
}

BitSet ecs_world_component_mask(const EcsWorld* world, const EcsArchetypeId archetype) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(
      sentinel_check(archetype) || (archetype < ecs_storage_archetype_count(&world->storage)),
      "{} is an invalid archetype",
      fmt_int(archetype));

  return ecs_storage_archetype_mask(&world->storage, archetype);
}

void ecs_world_flush(EcsWorld* world) {
  diag_assert_msg(!g_ecsRunningSystem, "World cannot be flushed from a system");
  diag_assert(!ecs_world_busy(world));

  ecs_world_flush_internal(world);
}

const EcsView* ecs_world_view_storage_internal(const EcsWorld* world) {
  return dynarray_begin_t(&world->views, EcsView);
}

void ecs_world_busy_set(EcsWorld* world) {
  diag_assert_msg(!ecs_world_busy(world), "World is already busy");
  world->flags |= EcsWorldFlags_Busy;
}

void ecs_world_busy_unset(EcsWorld* world) {
  diag_assert_msg(ecs_world_busy(world), "World is not busy");
  world->flags &= ~EcsWorldFlags_Busy;
}

void ecs_world_flush_internal(EcsWorld* world) {

  trace_begin("ecs_flush_new", TraceColor_White);
  ecs_storage_flush_new_entities(&world->storage);
  trace_end();

  BitSet      tmpMask     = ecs_comp_mask_stack(world->def);
  const usize bufferCount = ecs_buffer_count(&world->buffer);

  /**
   * Finalize (invoke destructors) components that have been removed this frame.
   */
  trace_begin("ecs_flush_finalize", TraceColor_White);
  for (usize i = 0; i != bufferCount; ++i) {
    const EcsEntityId entity = ecs_buffer_entity(&world->buffer, i);

    if (ecs_buffer_entity_flags(&world->buffer, i) & EcsBufferEntityFlags_Destroy) {
      const BitSet mask = ecs_storage_entity_mask(&world->storage, entity);
      ecs_storage_queue_finalize(&world->storage, &world->finalizer, entity, mask);
      // NOTE: Discard any component additions for the same entity in the buffer.
      ecs_world_queue_finalize_added(world, &world->buffer, i);
      continue;
    }

    ecs_world_removed_comps_mask(&world->buffer, i, tmpMask);
    ecs_storage_queue_finalize(&world->storage, &world->finalizer, entity, tmpMask);
  }
  ecs_finalizer_flush(&world->finalizer);
  trace_end();

  /**
   * Move entities to their new archetypes and apply the added components.
   */
  trace_begin("ecs_flush_move", TraceColor_White);
  for (usize i = 0; i != bufferCount; ++i) {
    const EcsEntityId entity = ecs_buffer_entity(&world->buffer, i);

    if (ecs_buffer_entity_flags(&world->buffer, i) & EcsBufferEntityFlags_Destroy) {
      ecs_storage_entity_destroy(&world->storage, entity);
      continue;
    }
    const BitSet curCompMask = ecs_storage_entity_mask(&world->storage, entity);
    ecs_world_new_comps_mask(&world->buffer, i, curCompMask, tmpMask);

    const EcsArchetypeId newArchetype = ecs_world_archetype_find_or_create(world, tmpMask);
    ecs_storage_entity_move(&world->storage, entity, newArchetype);
    ecs_world_apply_added_comps(&world->storage, &world->buffer, i, curCompMask);
  }
  trace_end();

  ecs_buffer_clear(&world->buffer);

  trace_begin("ecs_flush_views", TraceColor_White);
  dynarray_for_t(&world->views, EcsView, view) { ecs_view_flush(view); }
  trace_end();

  // Update stats.
  world->lastFlushEntities = (u32)bufferCount;
}

EcsWorldStats ecs_world_stats_query(const EcsWorld* world) {
  return (EcsWorldStats){
      .entityCount          = ecs_storage_entity_count(&world->storage),
      .archetypeCount       = ecs_storage_archetype_count(&world->storage),
      .archetypeEmptyCount  = ecs_storage_archetype_count_empty(&world->storage),
      .archetypeTotalSize   = (u32)ecs_storage_archetype_total_size(&world->storage),
      .archetypeTotalChunks = (u32)ecs_storage_archetype_total_chunks(&world->storage),
      .lastFlushEntities    = world->lastFlushEntities,
  };
}

u32 ecs_world_view_entities(const EcsWorld* world, const EcsViewId viewId) {
  diag_assert_msg(viewId < world->views.size, "Invalid view id: {}", fmt_int(viewId));
  const EcsView* view = dynarray_at_t(&world->views, viewId, EcsView);
  return ecs_view_entities(view);
}

u32 ecs_world_view_chunks(const EcsWorld* world, const EcsViewId viewId) {
  diag_assert_msg(viewId < world->views.size, "Invalid view id: {}", fmt_int(viewId));
  const EcsView* view = dynarray_at_t(&world->views, viewId, EcsView);
  return ecs_view_chunks(view);
}

u32 ecs_world_archetype_count(const EcsWorld* world) {
  return ecs_storage_archetype_count(&world->storage);
}

u32 ecs_world_archetype_count_with_comp(const EcsWorld* world, const EcsCompId comp) {
  return ecs_storage_archetype_count_with_comp(&world->storage, comp);
}

u32 ecs_world_archetype_entities(const EcsWorld* world, const EcsArchetypeId archetypeId) {
  return ecs_storage_archetype_entities(&world->storage, archetypeId);
}

u32 ecs_world_archetype_entities_per_chunk(
    const EcsWorld* world, const EcsArchetypeId archetypeId) {
  return ecs_storage_archetype_entities_per_chunk(&world->storage, archetypeId);
}

usize ecs_world_archetype_size(const EcsWorld* world, const EcsArchetypeId archetypeId) {
  return ecs_storage_archetype_size(&world->storage, archetypeId);
}

u32 ecs_world_archetype_chunks(const EcsWorld* world, const EcsArchetypeId archetypeId) {
  return ecs_storage_archetype_chunks(&world->storage, archetypeId);
}

u32 ecs_world_entity_count_with_comp(const EcsWorld* world, const EcsCompId comp) {
  return ecs_storage_entity_count_with_comp(&world->storage, comp);
}
