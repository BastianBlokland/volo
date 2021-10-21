#include "core_annotation.h"
#include "core_diag.h"
#include "core_thread.h"

#include "affinity_queue_internal.h"

#define item_wrap(_IDX_) ((_IDX_) & (affqueue_max_items - 1))

ASSERT((affqueue_max_items & (affqueue_max_items - 1u)) == 0, "Max size has to be a power-of-two")

AffQueue affqueue_create(Allocator* alloc) {
  return (AffQueue){
      .bottom = 0,
      .top    = 0,
      .items = alloc_alloc(alloc, sizeof(AffQueueItem) * affqueue_max_items, alignof(WorkItem)).ptr,
  };
}

void affqueue_destroy(Allocator* alloc, AffQueue* aq) {
  alloc_free(alloc, mem_create(aq->items, sizeof(AffQueueItem) * affqueue_max_items));
}

usize affqueue_size(const AffQueue* aq) { return (usize)(aq->top - aq->bottom); }

void affqueue_push(AffQueue* aq, Job* job, const JobTaskId task) {
  diag_assert_msg(
      affqueue_size(aq) < affqueue_max_items,
      "Maximum number of affinity-queue items ({}) has been exceeded",
      fmt_int(affqueue_max_items));

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
  for (i64 i = bottom; i != top; ++i) {
    AffQueueItem* item            = aq->items + item_wrap(i);
    i64           expectedHasData = true;
    if (LIKELY(thread_atomic_compare_exchange_i64(&item->hasData, &expectedHasData, false))) {
      ++aq->bottom;
      return item->work;
    }
  }
  return (WorkItem){0}; // Queue is empty.
}
