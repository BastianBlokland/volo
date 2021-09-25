#include "core_diag.h"
#include "ecs_runner.h"

#include "entity_allocator_internal.h"
#include "storage_internal.h"

// Note: Not a hard limit, will grow beyond this if needed.
#define ecs_starting_entities_capacity 1024

/**
 * Note: Only safe during a flush or the ecs_storage_create() function.
 */
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

/**
 * Note: Only safe to be called during a flush.
 */
static void ecs_storage_flush_new_entities(EcsStorage* storage) {
  dynarray_for_t(&storage->newEntities, EcsEntityId, newEntityId, {
    ecs_storage_entity_ensure(storage, ecs_entity_id_index(*newEntityId));
    ecs_storage_entity_init(storage, *newEntityId);
  });
  dynarray_clear(&storage->newEntities);
}

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
    // instead we defer the resizing until the end of the tick.
  }

  thread_spinlock_lock(&storage->newEntitiesLock);
  *dynarray_push_t(&storage->newEntities, EcsEntityId) = id;
  thread_spinlock_unlock(&storage->newEntitiesLock);
  return id;
}

EcsEntityInfo* ecs_storage_entity_info(EcsStorage* storage, EcsEntityId id) {
  if (UNLIKELY(ecs_entity_id_index(id) >= storage->entities.size)) {
    return null;
  }
  EcsEntityInfo* info = dynarray_at_t(&storage->entities, ecs_entity_id_index(id), EcsEntityInfo);
  return info->serial == ecs_entity_id_serial(id) ? info : null;
}

bool ecs_storage_entity_exists(const EcsStorage* storage, const EcsEntityId id) {
  if (ecs_entity_id_index(id) >= storage->entities.size) {
    // Out of bounds entity means it was created but not flushed yet.
    return true;
  }
  return ecs_storage_entity_info((EcsStorage*)storage, id) != null;
}

EcsArchetypeId ecs_storage_achetype_find(EcsStorage* storage, const BitSet mask) {
  dynarray_for_t(&storage->archetypes, EcsArchetype, arch, {
    if (mem_eq(arch->mask, mask)) {
      return (EcsArchetypeId)arch_i;
    }
  });
  return sentinel_u32;
}

EcsArchetypeId ecs_storage_archtype_find_or_create(EcsStorage* storage, const BitSet mask) {
  EcsArchetypeId res = ecs_storage_achetype_find(storage, mask);
  if (sentinel_check(res)) {
    res                                                  = (EcsArchetypeId)storage->archetypes.size;
    *dynarray_push_t(&storage->archetypes, EcsArchetype) = ecs_archetype_create(storage->def, mask);
  }
  return res;
}

void ecs_storage_flush(EcsStorage* storage) { ecs_storage_flush_new_entities(storage); }
