#pragma once
#include "core/alloc.h"

#include "work.h"

/**
 * Affinity Queue, queue for tasks that can only be run on a specific thread.
 *
 * It is a multi-producer single-consumer FIFO queue where all threads can push work but only the
 * owning thread can pop.
 */

#define affqueue_max_items 256

typedef struct {
  i64      hasData;
  WorkItem work;
} AffQueueItem;

typedef struct {
  i64           top, bottom;
  AffQueueItem* items;
} AffQueue;

AffQueue affqueue_create(Allocator*);
void     affqueue_destroy(Allocator*, AffQueue*);

/**
 * Push a new item to the queue.
 * NOTE: Can be called by any thread.
 */
void affqueue_push(AffQueue*, Job*, JobTaskId);

/**
 * Pop an item from the queue in a FIFO manner.
 * NOTE: Can only be called by the owning thread.
 */
WorkItem affqueue_pop(AffQueue*);
