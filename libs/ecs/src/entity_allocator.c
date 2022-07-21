#include "core_diag.h"

#include "entity_allocator_internal.h"

// Note: Not a hard limit, will grow beyond this if needed.
#define ecs_starting_free_indices 1024

ASSERT((ecs_starting_free_indices % 8) == 0, "ecs_starting_free_indices should be byte aligned");

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
  u64 serial, index;
  thread_spinlock_lock(&entityAllocator->lock);
  {
    // Note: 'thread_spinlock_lock()' includes a general memory barrier, so any writes done by other
    // 'entity_allocator_alloc' invocations on other threads are flushed.

    serial = ++entityAllocator->serialCounter;

    // Try to find a free index.
    index = dynbitset_next(&entityAllocator->freeIndices, 0);
    if (sentinel_check(index)) {
      // No existing free index found, add one at the end.
      index = entityAllocator->totalIndices++;
    } else {
      // Existing index found: Mark it as taken.
      dynbitset_clear(&entityAllocator->freeIndices, index);
    }
  }
  thread_spinlock_unlock(&entityAllocator->lock);

  diag_assert_msg(index < u32_max, "Entity indices exhausted");
  diag_assert_msg(serial < u32_max, "Entity serials exhausted");
  return (EcsEntityId)(index | (serial << 32u));
}

void entity_allocator_free(EntityAllocator* entityAllocator, const EcsEntityId id) {
  thread_spinlock_lock(&entityAllocator->lock);
  {
    diag_assert_msg(
        !dynbitset_test(&entityAllocator->freeIndices, ecs_entity_id_index(id)),
        "Entity {} was already freed before",
        fmt_int(id, .base = 16));

    // Mark the entity index as being free again (bit set to 1).
    // Note: This can grow the dynamic-bitset if this index was never free before.
    dynbitset_set(&entityAllocator->freeIndices, ecs_entity_id_index(id));
  }
  thread_spinlock_unlock(&entityAllocator->lock);
}

u32 entity_allocator_count_active(const EntityAllocator* entityAllocator) {
  u32 result;
  thread_spinlock_lock((ThreadSpinLock*)&entityAllocator->lock);
  {
    const usize totalFree = dynbitset_count(&entityAllocator->freeIndices);
    result                = (u32)(entityAllocator->totalIndices - totalFree);
  }
  thread_spinlock_unlock((ThreadSpinLock*)&entityAllocator->lock);
  return result;
}
