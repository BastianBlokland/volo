#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_runner.h"

#include "entity_allocator_internal.h"
#include "storage_internal.h"

EcsStorage* ecs_storage_create(Allocator* alloc) {
  EcsStorage* storage = alloc_alloc_t(alloc, EcsStorage);
  *storage            = (EcsStorage){
      .entityAllocator = entity_allocator_create(alloc),
      .memoryAllocator = alloc,
  };
  return storage;
}

void ecs_storage_destroy(EcsStorage* storage) {
  entity_allocator_destroy(&storage->entityAllocator);
  alloc_free_t(storage->memoryAllocator, storage);
}

EcsEntityId ecs_storage_entity_create(EcsStorage* storage) {
  // diag_assert(g_ecsRunningSystem); // TODO: Enable this once ecs systems api is in place.
  return entity_allocator_alloc(&storage->entityAllocator);
}
