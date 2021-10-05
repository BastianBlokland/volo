#include "core_diag.h"
#include "ecs_runner.h"

#include "archetype_internal.h"
#include "entity_allocator_internal.h"
#include "storage_internal.h"

// Note: Not a hard limit, will grow beyond this if needed.
#define ecs_starting_entities_capacity 1024

#define ecs_comp_mask_stack(_DEF_) mem_stack(bits_to_bytes(ecs_def_comp_count(_DEF_)) + 1)

typedef struct {
  u32            serial;
  EcsArchetypeId archetype;
  u32            archetypeIndex;
} EcsEntityInfo;

static void ecs_storage_entity_ensure(EcsStorage* storage, const u32 index) {
  if (UNLIKELY(index >= storage->entities.size)) {
    Mem entities = dynarray_push(&storage->entities, (index + 1) - storage->entities.size);
    mem_set(entities, 0); // mem_set() as 0 is an invalid entity serial number
  }
}

static void ecs_storage_entity_init(EcsStorage* storage, const EcsEntityId id) {
  EcsEntityInfo* info = dynarray_at_t(&storage->entities, ecs_entity_id_index(id), EcsEntityInfo);
  if (info->serial != ecs_entity_id_serial(id)) {
    *info = (EcsEntityInfo){
        .serial    = ecs_entity_id_serial(id),
        .archetype = sentinel_u32,
    };
  }
}

static EcsArchetype* ecs_storage_archetype_ptr(EcsStorage* storage, const EcsArchetypeId id) {
  if (sentinel_check(id)) {
    return null;
  }
  return dynarray_at_t(&storage->archetypes, id, EcsArchetype);
}

static EcsEntityInfo* ecs_storage_entity_info_ptr(EcsStorage* storage, const EcsEntityId id) {
  if (UNLIKELY(ecs_entity_id_index(id) >= storage->entities.size)) {
    return null;
  }
  EcsEntityInfo* info = dynarray_at_t(&storage->entities, ecs_entity_id_index(id), EcsEntityInfo);
  return info->serial == ecs_entity_id_serial(id) ? info : null;
}

i8 ecs_compare_archetype(const void* a, const void* b) { return compare_u32(a, b); }

EcsStorage ecs_storage_create(Allocator* alloc, const EcsDef* def) {
  diag_assert(alloc && def);

  EcsStorage storage = (EcsStorage){
      .def             = def,
      .entityAllocator = entity_allocator_create(alloc),
      .entities        = dynarray_create_t(alloc, EcsEntityInfo, ecs_starting_entities_capacity),
      .newEntities     = dynarray_create_t(alloc, EcsEntityId, 128),
      .archetypes      = dynarray_create_t(alloc, EcsArchetype, 128),
  };

  ecs_storage_entity_ensure(&storage, ecs_starting_entities_capacity);
  return storage;
}

void ecs_storage_destroy(EcsStorage* storage) {
  dynarray_for_t(&storage->archetypes, EcsArchetype, arch, { ecs_archetype_destroy(arch); });
  dynarray_destroy(&storage->archetypes);

  entity_allocator_destroy(&storage->entityAllocator);

  dynarray_destroy(&storage->entities);
  dynarray_destroy(&storage->newEntities);
}

EcsEntityId ecs_storage_entity_create(EcsStorage* storage) {
  const EcsEntityId id = entity_allocator_alloc(&storage->entityAllocator);

  if (ecs_entity_id_index(id) < storage->entities.size) {
    ecs_storage_entity_init(storage, id);
  } else {
    // Entity out of bounds, resizing the entities array here would require syncronization, so
    // instead we defer the resizing until the next flush.
  }

  thread_spinlock_lock(&storage->newEntitiesLock);
  *dynarray_push_t(&storage->newEntities, EcsEntityId) = id;
  thread_spinlock_unlock(&storage->newEntitiesLock);
  return id;
}

bool ecs_storage_entity_exists(const EcsStorage* storage, const EcsEntityId id) {
  if (ecs_entity_id_index(id) >= storage->entities.size) {
    // Out of bounds entity means it was created but not flushed yet.
    return true;
  }
  return ecs_storage_entity_info_ptr((EcsStorage*)storage, id) != null;
}

BitSet ecs_storage_entity_mask(EcsStorage* storage, const EcsEntityId id) {
  EcsEntityInfo* info = ecs_storage_entity_info_ptr(storage, id);
  if (!info) {
    return mem_empty;
  }
  EcsArchetype* archetype = ecs_storage_archetype_ptr(storage, info->archetype);
  if (!archetype) {
    return mem_empty;
  }
  return archetype->mask;
}

EcsArchetypeId ecs_storage_entity_archetype(EcsStorage* storage, const EcsEntityId id) {
  EcsEntityInfo* info = ecs_storage_entity_info_ptr(storage, id);
  return !info ? sentinel_u32 : info->archetype;
}

void ecs_storage_entity_move(
    EcsStorage* storage, const EcsEntityId id, const EcsArchetypeId newArchetypeId) {

  EcsEntityInfo* info              = ecs_storage_entity_info_ptr(storage, id);
  EcsArchetype*  oldArchetype      = ecs_storage_archetype_ptr(storage, info->archetype);
  const u32      oldArchetypeIndex = info->archetypeIndex;

  EcsArchetype* newArchetype = ecs_storage_archetype_ptr(storage, newArchetypeId);
  diag_assert_msg(newArchetype != oldArchetype, "Entity cannot be moved to the same archetype");

  if (newArchetype) {
    const u32 newArchetypeIndex = ecs_archetype_add(newArchetype, id);
    if (oldArchetype) {
      // Copy the components that both archetypes have in common.
      BitSet overlapping = ecs_comp_mask_stack(storage->def);
      mem_cpy(overlapping, oldArchetype->mask);
      bitset_and(overlapping, newArchetype->mask);

      ecs_archetype_copy_across(
          overlapping, newArchetype, newArchetypeIndex, oldArchetype, info->archetypeIndex);
    }
    info->archetype      = newArchetypeId;
    info->archetypeIndex = newArchetypeIndex;
  } else {
    info->archetype = sentinel_u32;
  }

  if (oldArchetype) {
    const EcsEntityId movedEntity = ecs_archetype_remove(oldArchetype, oldArchetypeIndex);
    if (ecs_entity_valid(movedEntity)) {
      ecs_storage_entity_info_ptr(storage, movedEntity)->archetypeIndex =
          (u32)oldArchetype->entityCount - 1;
    }
  }
}

void ecs_storage_entity_destroy(EcsStorage* storage, const EcsEntityId id) {

  EcsEntityInfo* info = ecs_storage_entity_info_ptr(storage, id);
  diag_assert_msg(info, "Missing entity-info for entity '{}'", fmt_int(id));

  EcsArchetype* archetype = ecs_storage_archetype_ptr(storage, info->archetype);
  if (archetype) {
    const EcsEntityId movedEntity = ecs_archetype_remove(archetype, info->archetypeIndex);
    if (ecs_entity_valid(movedEntity)) {
      ecs_storage_entity_info_ptr(storage, movedEntity)->archetypeIndex =
          (u32)archetype->entityCount - 1;
    }
  }

  info->serial = 0;
  entity_allocator_free(&storage->entityAllocator, id);
}

usize ecs_storage_archetype_entities_per_chunk(EcsStorage* storage, const EcsArchetypeId id) {
  return ecs_storage_archetype_ptr(storage, id)->entitiesPerChunk;
}

EcsArchetypeId ecs_storage_archetype_find(EcsStorage* storage, const BitSet mask) {
  dynarray_for_t(&storage->archetypes, EcsArchetype, arch, {
    if (mem_eq(arch->mask, mask)) {
      return (EcsArchetypeId)arch_i;
    }
  });
  return sentinel_u32;
}

EcsArchetypeId ecs_storage_archetype_create(EcsStorage* storage, const BitSet mask) {
  diag_assert_msg(bitset_any(mask), "Archetype needs atleast one component");
  diag_assert_msg(
      sentinel_check(ecs_storage_archetype_find(storage, mask)),
      "An archetype already exists with the same components");

  const EcsArchetypeId id                              = (EcsArchetypeId)storage->archetypes.size;
  *dynarray_push_t(&storage->archetypes, EcsArchetype) = ecs_archetype_create(storage->def, mask);
  return id;
}

bool ecs_storage_itr_walk(EcsStorage* storage, EcsIterator* itr, const EcsArchetypeId id) {
  return ecs_archetype_itr_walk(ecs_storage_archetype_ptr(storage, id), itr);
}

void ecs_storage_itr_jump(EcsStorage* storage, EcsIterator* itr, const EcsEntityId id) {
  EcsEntityInfo* info      = ecs_storage_entity_info_ptr(storage, id);
  EcsArchetype*  archetype = ecs_storage_archetype_ptr(storage, info->archetype);
  ecs_archetype_itr_jump(archetype, itr, info->archetypeIndex);
}

void ecs_storage_flush_new_entities(EcsStorage* storage) {
  dynarray_for_t(&storage->newEntities, EcsEntityId, newEntityId, {
    ecs_storage_entity_ensure(storage, ecs_entity_id_index(*newEntityId));
    ecs_storage_entity_init(storage, *newEntityId);
  });
  dynarray_clear(&storage->newEntities);
}
