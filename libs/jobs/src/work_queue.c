#include "core_annotation.h"
#include "core_diag.h"
#include "core_thread.h"

#include "work_queue_internal.h"

/**
 * The current implementation inserts way more memory barriers then are required (especially on
 * x86), reason is every atomic operation includes a general memory barrier at this time. This can
 * be greatly improved but requires careful examination what barriers are required for each
 * platform.
 */

#define item_wrap(_IDX_) ((_IDX_) & (workqueue_max_items - 1))

ASSERT((workqueue_max_items & (workqueue_max_items - 1u)) == 0, "Max size has to be a power-of-two")

/**
 * Amount of items in the queue, only an indication as it can be raced by the mutating apis.
 */
MAYBE_UNUSED static usize workqueue_size(const WorkQueue* wq) {
  const i64 bottom = wq->bottom;
  const i64 top    = wq->top;
  return (usize)(bottom >= top ? bottom - top : 0);
}

WorkQueue workqueue_create(Allocator* alloc) {
  return (WorkQueue){
      .bottom = 0,
      .top    = 0,
      .items  = alloc_array_t(alloc, WorkItem, workqueue_max_items),
  };
}

void workqueue_destroy(Allocator* alloc, WorkQueue* wq) {
  alloc_free_array_t(alloc, wq->items, workqueue_max_items);
}

void workqueue_push(WorkQueue* wq, Job* job, const JobTaskId task) {
  diag_assert_msg(
      workqueue_size(wq) < workqueue_max_items,
      "Maximum number of work-queue items ({}) has been exceeded",
      fmt_int(workqueue_max_items));

  const i64 idx             = wq->bottom; // No atomic load as its only written to from this thread.
  wq->items[item_wrap(idx)] = (WorkItem){
      .job  = job,
      .task = task,
  };
  thread_atomic_store_i64(&wq->bottom, idx + 1);
}

WorkItem workqueue_pop(WorkQueue* wq) {
  const i64 idx = wq->bottom - 1; // No atomic load as its only written to from this thread.
  thread_atomic_store_i64(&wq->bottom, idx);

  i64 topIdx = wq->top;
  if (topIdx > idx) {
    wq->bottom = idx + 1;
    return (WorkItem){0}; // Queue was already empty.
  }

  WorkItem item = wq->items[item_wrap(idx)];
  if (idx != topIdx) {
    return item; // More then one item left; we can just return the item.
  }

  // Last item; attempt to claim it.
  if (!thread_atomic_compare_exchange_i64(&wq->top, &topIdx, topIdx + 1)) {
    item = (WorkItem){0}; // Another thread stole it.
  }

  wq->bottom = idx + 1;
  return item;
}

WorkItem workqueue_steal(WorkQueue* wq) {
  i64       idx       = thread_atomic_load_i64(&wq->top);
  const i64 bottomIdx = thread_atomic_load_i64(&wq->bottom);

  if (idx >= bottomIdx) {
    return (WorkItem){0}; // Queue was already empty.
  }

  WorkItem item = wq->items[item_wrap(idx)];

  // Attempt to claim the item.
  if (!thread_atomic_compare_exchange_i64(&wq->top, &idx, idx + 1)) {
    return (WorkItem){0}; // A pop or another steal got it before us.
  }

  return item;
}
