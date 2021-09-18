#include "core_diag.h"

#include "entity_allocator_internal.h"

#define ecs_starting_free_indices (10 * 1024)

_Static_assert(
    (ecs_starting_free_indices % 8) == 0, "ecs_starting_free_indices should be byte aligned");

EntityAllocator entity_allocator_create(Allocator* alloc) {

  // Start with 'ecs_starting_free_indices' amount of free indices.
  DynBitSet freeIndices = dynbitset_create(alloc, ecs_starting_free_indices);
  dynbitset_set_all(&freeIndices, ecs_starting_free_indices);

  return (EntityAllocator){
      .freeIndices  = freeIndices,
      .totalIndices = ecs_starting_free_indices,
  };
}

void entity_allocator_destroy(EntityAllocator* entityAllocator) {
  dynbitset_destroy(&entityAllocator->freeIndices);
}

EcsEntityId entity_allocator_alloc(EntityAllocator* entityAllocator) {
  u32 serial, index;
  thread_spinlock_lock(&entityAllocator->lock);
  {
    serial = ++entityAllocator->serialCounter;

    // Try to find a free index.
    index = (u32)bitset_next(dynbitset_view(&entityAllocator->freeIndices), 0);
    if (sentinel_check(index)) {
      // No existing free index found, add one at the end.
      index = (u32)entityAllocator->totalIndices++;
    } else {
      // Existing index found: Mark it as taken.
      dynbitset_clear(&entityAllocator->freeIndices, index);
    }
  }
  thread_spinlock_unlock(&entityAllocator->lock);
  return (EcsEntityId)((u64)index | ((u64)serial << 32u));
}

void entity_allocator_free(EntityAllocator* entityAllocator, const EcsEntityId id) {
  thread_spinlock_lock(&entityAllocator->lock);
  {
    diag_assert_msg(
        !dynbitset_test(&entityAllocator->freeIndices, ecs_entity_id_index(id)),
        "Entity {} was already freed before",
        fmt_int(id));

    // Mark the entity index as being free again (bit set to 1).
    // Note: This can grow the dynamic-bitset if this index was never free before.
    dynbitset_set(&entityAllocator->freeIndices, ecs_entity_id_index(id));
  }
  thread_spinlock_unlock(&entityAllocator->lock);
}
