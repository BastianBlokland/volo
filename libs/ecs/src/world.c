#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_runner.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "def_internal.h"
#include "storage_internal.h"
#include "view_internal.h"
#include "world_internal.h"

typedef enum {
  EcsWorldFlags_None,
  EcsWorldFlags_Busy = 1 << 0, // For example set when a runner is active on this world.
} EcsWorldFlags;

struct sEcsWorld {
  const EcsDef* def;
  EcsStorage    storage;
  DynArray      views; // EcsView[].

  ThreadSpinLock bufferLock;
  EcsBuffer      buffer;

  EcsWorldFlags flags;
  Allocator*    alloc;
};

#define ecs_comp_mask_stack(_DEF_) mem_stack(bits_to_bytes(ecs_def_comp_count(_DEF_)) + 1)

static usize
ecs_world_archetype_track(EcsWorld* world, const EcsArchetypeId id, const BitSet mask) {
  usize trackingViews = 0;
  dynarray_for_t(&world->views, EcsView, view, {
    if (ecs_view_maybe_track(view, id, mask)) {
      ++trackingViews;
    }
  });
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

  log_d(
      "Ecs archetype created",
      log_param("components", fmt_int(bitset_count(mask))),
      log_param(
          "entities-per-chunk",
          fmt_int(ecs_storage_archetype_entities_per_chunk(&world->storage, newId))),
      log_param("tracking-views", fmt_int(trackingViews)));

  (void)trackingViews;
  return newId;
}

static void ecs_world_cpy_added_comps(EcsStorage* storage, EcsBuffer* buffer, const usize idx) {

  const EcsEntityId entity     = ecs_buffer_entity(buffer, idx);
  const BitSet      addedComps = ecs_buffer_entity_added(buffer, idx);
  if (!bitset_any(addedComps)) {
    return;
  }

  /**
   * NOTE: 'addedComps' can contain empty components which are not present in the
   * 'ecs_buffer_comp_begin' / 'ecs_buffer_comp_next' iteration.
   */

  EcsIterator* storageItr = ecs_iterator_stack(addedComps);
  ecs_storage_itr_jump(storage, storageItr, entity);

  for (EcsBufferCompData* bufferItr = ecs_buffer_comp_begin(buffer, idx); bufferItr;
       bufferItr                    = ecs_buffer_comp_next(bufferItr)) {

    const EcsCompId compId   = ecs_buffer_comp_id(bufferItr);
    const Mem       compData = ecs_buffer_comp_data(buffer, bufferItr);

    mem_cpy(ecs_iterator_access(storageItr, compId), compData);
  }
}

static void ecs_world_destroy_added_comps(const EcsDef* def, EcsBuffer* buffer, const usize idx) {

  for (EcsBufferCompData* bufferItr = ecs_buffer_comp_begin(buffer, idx); bufferItr;
       bufferItr                    = ecs_buffer_comp_next(bufferItr)) {

    const EcsCompId         compId     = ecs_buffer_comp_id(bufferItr);
    const Mem               compData   = ecs_buffer_comp_data(buffer, bufferItr);
    const EcsCompDestructor destructor = ecs_def_comp_destructor(def, compId);
    if (destructor) {
      destructor(compData.ptr);
    }
  }
}

EcsWorld* ecs_world_create(Allocator* alloc, const EcsDef* def) {
  ecs_def_freeze((EcsDef*)def);

  EcsWorld* world = alloc_alloc_t(alloc, EcsWorld);
  *world          = (EcsWorld){
      .def     = def,
      .storage = ecs_storage_create(alloc, def),
      .views   = dynarray_create_t(alloc, EcsView, ecs_def_view_count(def)),
      .buffer  = ecs_buffer_create(alloc, def),
      .alloc   = alloc,
  };

  dynarray_for_t((DynArray*)&def->views, EcsViewDef, viewDef, {
    *dynarray_push_t(&world->views, EcsView) =
        ecs_view_create(alloc, &world->storage, def, viewDef);
  });

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

  ecs_storage_destroy(&world->storage);

  dynarray_for_t(
      &world->views, EcsView, view, { ecs_view_destroy(world->alloc, world->def, view); });
  dynarray_destroy(&world->views);

  ecs_buffer_destroy(&world->buffer);

  alloc_free_t(world->alloc, world);
}

const EcsDef* ecs_world_def(EcsWorld* world) { return world->def; }

bool ecs_world_busy(const EcsWorld* world) { return (world->flags & EcsWorldFlags_Busy) != 0; }

EcsView* ecs_world_view(EcsWorld* world, const EcsViewId view) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(
      !g_ecsRunningSystem || ecs_def_system_has_access(world->def, g_ecsRunningSystemId, view),
      "System {} has not declared access to view {}",
      fmt_text(ecs_def_system_name(world->def, g_ecsRunningSystemId)),
      fmt_text(ecs_def_view_name(world->def, view)));

  return dynarray_at_t(&world->views, view, EcsView);
}

EcsEntityId ecs_world_entity_create(EcsWorld* world) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  return ecs_storage_entity_create(&world->storage);
}

void ecs_world_entity_destroy(EcsWorld* world, const EcsEntityId entity) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  diag_assert_msg(
      ecs_world_exists(world, entity),
      "Unable to enqueue destruction of entity {}, reason: entity does not exist",
      fmt_int(entity));

  thread_spinlock_lock(&world->bufferLock);
  ecs_buffer_destroy_entity(&world->buffer, entity);
  thread_spinlock_unlock(&world->bufferLock);
}

bool ecs_world_exists(const EcsWorld* world, const EcsEntityId entity) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  return ecs_storage_entity_exists(&world->storage, entity);
}

bool ecs_world_has(EcsWorld* world, const EcsEntityId entity, const EcsCompId comp) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  diag_assert_msg(
      ecs_world_exists(world, entity),
      "Unable to check for {} on entity {}, reason: entity does not exist",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  const BitSet entityMask = ecs_storage_entity_mask(&world->storage, entity);
  return bitset_test(entityMask, comp);
}

void* ecs_world_add(
    EcsWorld* world, const EcsEntityId entity, const EcsCompId comp, const Mem data) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  diag_assert_msg(
      ecs_world_exists(world, entity),
      "Unable to add {} to entity {}, reason: entity does not exist",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  diag_assert_msg(
      !data.size || !ecs_world_has(world, entity, comp),
      "Unable to add {} to entity {}, reason: entity allready has the specified component",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  thread_spinlock_lock(&world->bufferLock);
  void* result = ecs_buffer_comp_add(&world->buffer, entity, comp, data);
  thread_spinlock_unlock(&world->bufferLock);
  return result;
}

void ecs_world_remove(EcsWorld* world, const EcsEntityId entity, const EcsCompId comp) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  diag_assert_msg(
      ecs_world_exists(world, entity),
      "Unable to remove {} from entity {}, reason: entity does not exist",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  diag_assert_msg(
      ecs_world_has(world, entity, comp),
      "Unable to remove {} from entity {}, reason: entity does not have the specified component",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  thread_spinlock_lock(&world->bufferLock);
  ecs_buffer_comp_remove(&world->buffer, entity, comp);
  thread_spinlock_unlock(&world->bufferLock);
}

void ecs_world_flush(EcsWorld* world) {
  diag_assert_msg(!g_ecsRunningSystem, "World cannot be flushed from a system");
  diag_assert(!ecs_world_busy(world));

  ecs_world_flush_internal(world);
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
  ecs_storage_flush_new_entities(&world->storage);

  BitSet      newCompMask = ecs_comp_mask_stack(world->def);
  const usize bufferCount = ecs_buffer_count(&world->buffer);

  for (usize i = 0; i != bufferCount; ++i) {
    const EcsEntityId entity = ecs_buffer_entity(&world->buffer, i);

    if (ecs_buffer_entity_flags(&world->buffer, i) & EcsBufferEntityFlags_Destroy) {
      /**
       * Discard any component additions for the same entity in the buffer.
       */
      ecs_world_destroy_added_comps(world->def, &world->buffer, i);

      ecs_storage_entity_destroy(&world->storage, entity);
      continue;
    }

    bitset_clear_all(newCompMask);
    bitset_or(newCompMask, ecs_storage_entity_mask(&world->storage, entity));
    bitset_or(newCompMask, ecs_buffer_entity_added(&world->buffer, i));
    bitset_xor(newCompMask, ecs_buffer_entity_removed(&world->buffer, i));

    const EcsArchetypeId newArchetype = ecs_world_archetype_find_or_create(world, newCompMask);
    if (ecs_storage_entity_archetype(&world->storage, entity) != newArchetype) {
      ecs_storage_entity_move(&world->storage, entity, newArchetype);
      ecs_world_cpy_added_comps(&world->storage, &world->buffer, i);
    }
  }

  ecs_buffer_clear(&world->buffer);
}
