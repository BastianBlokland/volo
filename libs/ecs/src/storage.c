#include "core_bits.h"
#include "core_diag.h"
#include "ecs_runner.h"

#include "archetype_internal.h"
#include "def_internal.h"
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

static EcsArchetype* ecs_storage_archetype_ptr(const EcsStorage* storage, const EcsArchetypeId id) {
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

static void ecs_storage_queue_finalize_itr(EcsFinalizer* finalizer, EcsIterator* itr) {
  EcsCompId compId = 0;
  for (usize i = 0; i != itr->compCount; ++i, ++compId) {
    compId = (EcsCompId)bitset_next(itr->mask, compId);
    ecs_finalizer_push(finalizer, compId, itr->comps[i].ptr);
  }
}

static void ecs_storage_queue_finalize_archetype(
    EcsStorage* storage, EcsFinalizer* finalizer, const EcsArchetypeId id) {

  EcsArchetype* archetype = ecs_storage_archetype_ptr(storage, id);
  EcsIterator*  itr       = ecs_iterator_stack(archetype->mask);
  while (ecs_storage_itr_walk(storage, itr, id)) {
    ecs_storage_queue_finalize_itr(finalizer, itr);
  }
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
  dynarray_for_t(&storage->archetypes, EcsArchetype, arch) { ecs_archetype_destroy(arch); }
  dynarray_destroy(&storage->archetypes);

  entity_allocator_destroy(&storage->entityAllocator);

  dynarray_destroy(&storage->entities);
  dynarray_destroy(&storage->newEntities);
}

void ecs_storage_queue_finalize(
    EcsStorage* storage, EcsFinalizer* finalizer, const EcsEntityId id, const BitSet mask) {

  EcsEntityInfo* info = ecs_storage_entity_info_ptr(storage, id);
  diag_assert_msg(info, "Missing entity-info for entity '{}'", fmt_int(id));

  EcsArchetype* archetype = ecs_storage_archetype_ptr(storage, info->archetype);
  if (archetype) {
    EcsIterator* itr = ecs_iterator_stack(mask);
    ecs_archetype_itr_jump(archetype, itr, info->archetypeIndex);
    ecs_storage_queue_finalize_itr(finalizer, itr);
  }
}

void ecs_storage_queue_finalize_all(EcsStorage* storage, EcsFinalizer* finalizer) {
  for (EcsArchetypeId archId = 0; archId != storage->archetypes.size; ++archId) {
    ecs_storage_queue_finalize_archetype(storage, finalizer, archId);
  }
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

u32 ecs_storage_entity_count(const EcsStorage* storage) {
  return entity_allocator_count_active(&storage->entityAllocator);
}

u32 ecs_storage_entity_count_with_comp(const EcsStorage* storage, const EcsCompId comp) {
  u32 count = 0;
  dynarray_for_t(&storage->archetypes, EcsArchetype, arch) {
    count += bitset_test(arch->mask, comp) * arch->entityCount;
  }
  return count;
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
  EcsArchetype*  newArchetype      = ecs_storage_archetype_ptr(storage, newArchetypeId);

  if (newArchetype == oldArchetype) {
    return; // Same archetype; no need to move.
  }

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
      ecs_storage_entity_info_ptr(storage, movedEntity)->archetypeIndex = oldArchetypeIndex;
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
      ecs_storage_entity_info_ptr(storage, movedEntity)->archetypeIndex = info->archetypeIndex;
    }
  }

  info->serial = 0;
  entity_allocator_free(&storage->entityAllocator, id);
}

u32 ecs_storage_archetype_count(const EcsStorage* storage) { return (u32)storage->archetypes.size; }

u32 ecs_storage_archetype_count_empty(const EcsStorage* storage) {
  u32 count = 0;
  dynarray_for_t(&storage->archetypes, EcsArchetype, arch) { count += arch->entityCount == 0; }
  return count;
}

u32 ecs_storage_archetype_count_with_comp(const EcsStorage* storage, const EcsCompId comp) {
  u32 count = 0;
  dynarray_for_t(&storage->archetypes, EcsArchetype, arch) {
    count += bitset_test(arch->mask, comp);
  }
  return count;
}

usize ecs_storage_archetype_total_size(const EcsStorage* storage) {
  usize result = 0;
  dynarray_for_t(&storage->archetypes, EcsArchetype, arch) {
    result += ecs_archetype_total_size(arch);
  }
  return result;
}

u32 ecs_storage_archetype_total_chunks(const EcsStorage* storage) {
  u32 result = 0;
  dynarray_for_t(&storage->archetypes, EcsArchetype, arch) { result += (u32)arch->chunkCount; }
  return result;
}

usize ecs_storage_archetype_entities_per_chunk(const EcsStorage* storage, const EcsArchetypeId id) {
  return ecs_storage_archetype_ptr(storage, id)->entitiesPerChunk;
}

EcsArchetypeId ecs_storage_archetype_find(EcsStorage* storage, const BitSet mask) {
  for (EcsArchetypeId archId = 0; archId != storage->archetypes.size; ++archId) {
    EcsArchetype* arch = dynarray_at_t(&storage->archetypes, archId, EcsArchetype);
    if (mem_eq(arch->mask, mask)) {
      return archId;
    }
  }
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
  dynarray_for_t(&storage->newEntities, EcsEntityId, newEntityId) {
    ecs_storage_entity_ensure(storage, ecs_entity_id_index(*newEntityId));
    ecs_storage_entity_init(storage, *newEntityId);
  }
  dynarray_clear(&storage->newEntities);
}
