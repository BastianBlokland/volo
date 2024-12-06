#include "core.h"
#include "core_diag.h"
#include "core_thread.h"

#include "affinity_queue_internal.h"

#include <immintrin.h>

#define item_wrap(_IDX_) ((_IDX_) & (affqueue_max_items - 1))

ASSERT((affqueue_max_items & (affqueue_max_items - 1u)) == 0, "Max size has to be a power-of-two")

AffQueue affqueue_create(Allocator* alloc) {
  Mem items = alloc_alloc(alloc, sizeof(AffQueueItem) * affqueue_max_items, alignof(AffQueueItem));
  mem_set(items, 0);
  return (AffQueue){
      .bottom = 0,
      .top    = 0,
      .items  = items.ptr,
  };
}

void affqueue_destroy(Allocator* alloc, AffQueue* aq) {
  alloc_free_array_t(alloc, aq->items, affqueue_max_items);
}

void affqueue_push(AffQueue* aq, Job* job, const JobTaskId task) {
  const i64     idx  = thread_atomic_add_i64(&aq->top, 1);
  AffQueueItem* item = aq->items + item_wrap(idx);
  item->work         = (WorkItem){
              .job  = job,
              .task = task,
  };
  thread_atomic_store_i64(&item->hasData, true);
}

WorkItem affqueue_pop(AffQueue* aq) {
  const i64 bottom = aq->bottom; // No atomic load as its only written to from this thread.
  const i64 top    = thread_atomic_load_i64(&aq->top);
  if (bottom == top) {
    return (WorkItem){0}; // Queue is empty.
  }

  AffQueueItem* item            = aq->items + item_wrap(bottom);
  i64           expectedHasData = true;
  while (!thread_atomic_compare_exchange_i64(&item->hasData, &expectedHasData, false)) {
    _mm_pause();
    expectedHasData = true;
  }
  ++aq->bottom;
  return item->work;
}
