#pragma once
#include "core_alloc.h"

#include "job_internal.h"

/**
 * Lock free single-producer multiple-consumer queue.
 *
 * The owner thread can push and pop from the LIFO end of the queue while other threads can steal
 * from the FIFO end.
 *
 * References:
 * - https://fzn.fr/readings/ppopp13.pdf
 * - https://github.com/taskflow/work-stealing-queue
 */

#define workqueue_max_items 8192

typedef struct {
  Job*      job;
  JobTaskId task;
} WorkItem;

typedef struct {
  i64       top, bottom;
  WorkItem* items;
} WorkQueue;

#define workitem_valid(_WORKITEM_) ((_WORKITEM_).job != null)

WorkQueue workqueue_create(Allocator*);
void      workqueue_destroy(Allocator*, WorkQueue*);

/**
 * Amount of items currently in the queue, only an indication as it can be raced by the mutating
 * apis.
 */
usize workqueue_size(const WorkQueue*);

/**
 * Push a new item to the queue.
 * NOTE: Can only be called by the owning thread.
 */
void workqueue_push(WorkQueue*, Job*, JobTaskId);

/**
 * Pop an item from the queue in a LIFO manner.
 * NOTE: Can only be called by the owning thread.
 */
WorkItem workqueue_pop(WorkQueue*);

/**
 * Pop an item from the queue in a FIFO manner.
 * NOTE: Can be called by any thread.
 */
WorkItem workqueue_steal(WorkQueue*);
