#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_runner.h"

#include "buffer_internal.h"
#include "def_internal.h"
#include "storage_internal.h"
#include "world_internal.h"

typedef enum {
  EcsWorldFlags_None,
  EcsWorldFlags_Busy = 1 << 0, // For example set when a runner is active on this world.
} EcsWorldFlags;

struct sEcsWorld {
  const EcsDef* def;
  EcsStorage    storage;

  ThreadSpinLock bufferLock;
  EcsBuffer      buffer;

  EcsWorldFlags flags;
  Allocator*    alloc;
};

#define ecs_comp_mask_stack(_DEF_) mem_stack(bits_to_bytes(ecs_def_comp_count(_DEF_)) + 1)

EcsWorld* ecs_world_create(Allocator* alloc, const EcsDef* def) {
  ecs_def_freeze((EcsDef*)def);

  EcsWorld* world = alloc_alloc_t(alloc, EcsWorld);
  *world          = (EcsWorld){
      .def     = def,
      .storage = ecs_storage_create(alloc, def),
      .buffer  = ecs_buffer_create(alloc, def),
      .alloc   = alloc,
  };

  return world;
}

void ecs_world_destroy(EcsWorld* world) {
  ecs_def_unfreeze((EcsDef*)world->def);

  ecs_storage_destroy(&world->storage);
  ecs_buffer_destroy(&world->buffer);
  alloc_free_t(world->alloc, world);
}

const EcsDef* ecs_world_def(EcsWorld* world) { return world->def; }

bool ecs_world_busy(const EcsWorld* world) { return (world->flags & EcsWorldFlags_Busy) != 0; }

EcsEntityId ecs_world_entity_create(EcsWorld* world) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  return ecs_storage_entity_create(&world->storage);
}

bool ecs_world_entity_exists(const EcsWorld* world, const EcsEntityId entity) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  return ecs_storage_entity_exists(&world->storage, entity);
}

void ecs_world_entity_destroy(EcsWorld* world, const EcsEntityId entity) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  diag_assert_msg(
      ecs_world_entity_exists(world, entity),
      "Unable to enqueue destruction of entity {}, reason: entity does not exist",
      fmt_int(entity));

  thread_spinlock_lock(&world->bufferLock);
  ecs_buffer_destroy_entity(&world->buffer, entity);
  thread_spinlock_unlock(&world->bufferLock);
}

bool ecs_world_comp_has(EcsWorld* world, const EcsEntityId entity, const EcsCompId comp) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  diag_assert_msg(
      ecs_world_entity_exists(world, entity),
      "Unable to check for {} on entity {}, reason: entity does not exist",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  const BitSet entityMask = ecs_storage_entity_mask(&world->storage, entity);
  return bitset_test(entityMask, comp);
}

void* ecs_world_comp_add(
    EcsWorld* world, const EcsEntityId entity, const EcsCompId comp, const Mem data) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  diag_assert_msg(
      ecs_world_entity_exists(world, entity),
      "Unable to add {} to entity {}, reason: entity does not exist",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  diag_assert_msg(
      !ecs_world_comp_has(world, entity, comp),
      "Unable to add {} to entity {}, reason: entity allready has the specified component",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  thread_spinlock_lock(&world->bufferLock);
  void* result = ecs_buffer_comp_add(&world->buffer, entity, comp, data);
  thread_spinlock_unlock(&world->bufferLock);
  return result;
}

void ecs_world_comp_remove(EcsWorld* world, const EcsEntityId entity, const EcsCompId comp) {
  diag_assert(!ecs_world_busy(world) || g_ecsRunningSystem);
  diag_assert_msg(ecs_entity_valid(entity), "{} is an invalid entity", fmt_int(entity));

  diag_assert_msg(
      ecs_world_entity_exists(world, entity),
      "Unable to remove {} from entity {}, reason: entity does not exist",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  diag_assert_msg(
      ecs_world_comp_has(world, entity, comp),
      "Unable to remove {} from entity {}, reason: entity does not have the specified component",
      fmt_text(ecs_def_comp_name(world->def, comp)),
      fmt_int(entity));

  thread_spinlock_lock(&world->bufferLock);
  ecs_buffer_comp_remove(&world->buffer, entity, comp);
  thread_spinlock_unlock(&world->bufferLock);
}

void ecs_world_busy_set(EcsWorld* world) {
  diag_assert_msg(!ecs_world_busy(world), "World is already busy");
  world->flags |= EcsWorldFlags_Busy;
}

void ecs_world_busy_unset(EcsWorld* world) {
  diag_assert_msg(ecs_world_busy(world), "World is not busy");
  world->flags &= ~EcsWorldFlags_Busy;
}

void ecs_world_flush(EcsWorld* world) {
  diag_assert_msg(!g_ecsRunningSystem, "World cannot be flushed from a system");

  ecs_storage_flush_new_entities(&world->storage);

  BitSet      newCompMask = ecs_comp_mask_stack(world->def);
  const usize bufferCount = ecs_buffer_count(&world->buffer);

  for (usize i = 0; i != bufferCount; ++i) {
    const EcsEntityId entity = ecs_buffer_entity(&world->buffer, i);

    if (ecs_buffer_entity_flags(&world->buffer, i) & EcsBufferEntityFlags_Destroy) {
      ecs_storage_entity_destroy(&world->storage, entity);
      continue;
    }

    bitset_clear_all(newCompMask);
    bitset_or(newCompMask, ecs_storage_entity_mask(&world->storage, entity));
    bitset_or(newCompMask, ecs_buffer_entity_added(&world->buffer, i));
    bitset_xor(newCompMask, ecs_buffer_entity_removed(&world->buffer, i));

    const EcsArchetypeId newArchetype =
        ecs_storage_archtype_find_or_create(&world->storage, newCompMask);

    ecs_storage_entity_move(&world->storage, entity, newArchetype);

    // Initialize the added components.
    for (EcsBufferCompData* itr = ecs_buffer_comp_begin(&world->buffer, i); itr;
         itr                    = ecs_buffer_comp_next(itr)) {
      const EcsCompId compId        = ecs_buffer_comp_id(itr);
      const Mem       compData      = ecs_buffer_comp_data(&world->buffer, itr);
      void*           archetypeComp = ecs_storage_entity_comp(&world->storage, entity, compId);
      mem_cpy(mem_create(archetypeComp, compData.size), compData);
    }
  }

  ecs_buffer_clear(&world->buffer);
}
