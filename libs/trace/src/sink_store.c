#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "core_time.h"
#include "trace_sink.h"
#include "trace_sink_store.h"

#include "tracer_internal.h"

/**
 * Trace sink implementation that stores events in in-memory buffers to be queried later.
 */

#define trace_store_max_ids 16
#define trace_store_max_threads 8
#define trace_store_buffer_events 1024
#define trace_store_buffer_max_depth 8

ASSERT(trace_store_max_ids < u8_max, "Trace id has to be representable by a u8")
ASSERT((trace_store_buffer_events & (trace_store_buffer_events - 1u)) == 0, "Has to be a pow2");
ASSERT(trace_store_buffer_events < u16_max, "Events have to be representable with a u16")

static THREAD_LOCAL bool g_traceStoreIsVisiting;

typedef struct {
  String name;

  ThreadMutex resetLock; // Lock to avoid observing a buffer while its being reset.

  u16 stackCount;
  u16 stack[trace_store_buffer_max_depth];

  u16             eventCursor;
  TraceStoreEvent events[trace_store_buffer_events];

} TraceBuffer;

typedef struct {
  TraceSink   api;
  Allocator*  alloc;
  ThreadMutex storeLock;

  u32        idCount;
  StringHash idHashes[trace_store_max_ids];

  u32          threadCount;
  ThreadId     threadIds[trace_store_max_threads];
  TraceBuffer* threadBuffers[trace_store_max_threads];

} TraceSinkStore;

static u8 trace_id_find(TraceSinkStore* s, const StringHash hash) {
  for (u8 i = 0; i != s->idCount; ++i) {
    if (s->idHashes[i] == hash) {
      return i;
    }
  }
  return sentinel_u8;
}

NO_INLINE_HINT static u8 trace_id_add(TraceSinkStore* s, const StringHash hash, const String str) {
  thread_mutex_lock(s->storeLock);

  // After taking the lock check if another thread already added it, if so return that id.
  u8 result = trace_id_find(s, hash);
  if (sentinel_check(result)) {
    // Id has not been added yet; add it now.
    result = (u8)s->idCount;
    if (UNLIKELY(result == trace_store_max_ids)) {
      diag_crash_msg("trace: Maximum unique event ids exceeded");
    }
    s->idHashes[result] = hash;
    ++s->idCount;

    // Store the name in the global string-table so we can query for it later.
    stringtable_add(g_stringtable, str);
  }

  thread_mutex_unlock(s->storeLock);
  return result;
}

static u8 trace_id_register(TraceSinkStore* s, const String str) {
  const StringHash hash   = string_hash(str);
  const u8         result = trace_id_find(s, hash);
  if (LIKELY(!sentinel_check(result))) {
    return result;
  }
  return trace_id_add(s, hash, str);
}

static TraceBuffer* trace_buffer_find(TraceSinkStore* s, const ThreadId tid) {
  for (u32 i = 0; i != s->threadCount; ++i) {
    if (s->threadIds[i] == tid) {
      return s->threadBuffers[i];
    }
  }
  return null;
}

NO_INLINE_HINT static TraceBuffer* trace_buffer_add(TraceSinkStore* s, const ThreadId tid) {
  thread_mutex_lock(s->storeLock);
  TraceBuffer* result = null;

  // NOTE: We access thread-local data from the thread so we need to be called from that thread.
  diag_assert(tid == g_thread_tid);

  // Check if there's a thread that has exited, if so we can re-use its buffer.
  for (u32 i = 0; i != s->threadCount; ++i) {
    if (!thread_exists(s->threadIds[i])) {
      /**
       * TODO: The nested locks are not very elegant (and can stall all events while a potential
       * slow visit is happening), at the moment we assume that starting / stopping threads is rare.
       */
      thread_mutex_lock(s->threadBuffers[i]->resetLock);
      {
        s->threadIds[i] = tid;
        result          = s->threadBuffers[i];

        diag_assert(result->stackCount == 0);
        string_maybe_free(s->alloc, result->name);

        result->name        = string_maybe_dup(s->alloc, g_thread_name);
        result->eventCursor = 0;
        mem_set(array_mem(result->events), 0);
      }
      thread_mutex_unlock(s->threadBuffers[i]->resetLock);
      break;
    }
  }

  // If no buffer that can be re-used was found, then create a new buffer.
  if (!result) {
    if (UNLIKELY(s->threadCount == trace_store_max_threads)) {
      diag_crash_msg("trace: Maximum thread-count exceeded");
    }
    result              = alloc_alloc_t(s->alloc, TraceBuffer);
    result->name        = string_maybe_dup(s->alloc, g_thread_name);
    result->resetLock   = thread_mutex_create(s->alloc);
    result->eventCursor = result->stackCount = 0;
    mem_set(array_mem(result->events), 0);

    s->threadIds[s->threadCount]     = tid;
    s->threadBuffers[s->threadCount] = result;
    ++s->threadCount;
  }

  thread_mutex_unlock(s->storeLock);
  return result;
}

static TraceBuffer* trace_buffer_register(TraceSinkStore* s, const ThreadId tid) {
  TraceBuffer* result = trace_buffer_find(s, tid);
  if (LIKELY(result)) {
    return result;
  }
  return trace_buffer_add(s, tid);
}

static void trace_buffer_advance(TraceBuffer* b) {
  b->eventCursor = (b->eventCursor + 1) & (trace_store_buffer_events - 1);
}

static void trace_sink_store_event_begin(
    TraceSink* sink, const String id, const TraceColor color, const String msg) {
  TraceSinkStore* s = (TraceSinkStore*)sink;
  TraceBuffer*    b = trace_buffer_register(s, g_thread_tid);

  /**
   * Check that the current thread is not visiting, this could cause a deadlock when trying to reuse
   * a buffer that we are currently inspecting (holding the 'resetLock').
   */
  if (UNLIKELY(g_traceStoreIsVisiting)) {
    diag_crash_msg("trace: Unable to begin a new event while visiting");
  }

  // Initialize the event at the cursor.
  TraceStoreEvent* evt = &b->events[b->eventCursor];
  thread_spinlock_lock(&evt->lock);
  {
    evt->timeDur   = 0;
    evt->timeStart = time_steady_clock();
    evt->id        = trace_id_register(s, id);
    evt->color     = (u8)color;
    evt->msgLength = (u8)math_min(msg.size, array_elems(evt->msgData));
    mem_cpy(array_mem(evt->msgData), mem_create(msg.ptr, evt->msgLength));
  }
  thread_spinlock_unlock(&evt->lock);

  // Add it to the stack.
  b->stack[b->stackCount++] = b->eventCursor;
  if (UNLIKELY(b->stackCount > trace_store_buffer_max_depth)) {
    diag_crash_msg("trace: Trace event exceeded the maximum stack depth");
  }

  // Advance the cursor.
  trace_buffer_advance(b);
}

static void trace_sink_store_event_end(TraceSink* sink) {
  TraceSinkStore* s = (TraceSinkStore*)sink;
  TraceBuffer*    b = trace_buffer_find(s, g_thread_tid);
  diag_assert_msg(b && b->stackCount, "trace: Event ended that never started on this thread");

  // Pop the top-most event from the stack.
  TraceStoreEvent* evt = &b->events[b->stack[--b->stackCount]];
  diag_assert_msg(!evt->timeDur, "trace: Event ended twice");

  // Compute the event time.
  const TimeDuration dur = time_steady_duration(evt->timeStart, time_steady_clock());

  /**
   * NOTE: If the platforms timer granularity is imprecise then the duration can actually be
   * reported as 0 nano-seconds, to avoid this we make sure its always at least 1 ns.
   */
  evt->timeDur = math_max((u32)dur, 1);
}

static void trace_sink_store_destroy(TraceSink* sink) {
  TraceSinkStore* s = (TraceSinkStore*)sink;
  for (u32 i = 0; i != s->threadCount; ++i) {
    diag_assert(!s->threadBuffers[i]->stackCount);
    string_maybe_free(s->alloc, s->threadBuffers[i]->name);
    thread_mutex_destroy(s->threadBuffers[i]->resetLock);
    alloc_free_t(s->alloc, s->threadBuffers[i]);
  }
  thread_mutex_destroy(s->storeLock);
  alloc_free_t(s->alloc, s);
}

static bool trace_sink_is_store(const TraceSink* sink) {
  return sink->destroy == trace_sink_store_destroy;
}

void trace_sink_store_visit(const TraceSink* sink, const TraceStoreVisitor visitor, void* userCtx) {
  diag_assert_msg(trace_sink_is_store(sink), "Given sink is not a store-sink");
  const TraceSinkStore* s = (const TraceSinkStore*)sink;

  g_traceStoreIsVisiting = true;

  TraceStoreEvent evt;

  for (u32 bufferIdx = 0; bufferIdx != s->threadCount; ++bufferIdx) {
    const ThreadId tid = s->threadIds[bufferIdx];
    TraceBuffer*   b   = s->threadBuffers[bufferIdx];
    thread_mutex_lock(b->resetLock); // Avoid observing while the buffer is being reset.

    /**
     * Start reading as far away from the write-cursor as possible to reduce contention.
     * NOTE: This means the events are visited out of chronological order.
     */
    const u32 eventCountHalf = trace_store_buffer_events / 2;
    const u32 readCursor     = (b->eventCursor + eventCountHalf) & (trace_store_buffer_events - 1);

    for (u32 i = 0; i != trace_store_buffer_events; ++i) {
      const u32 eventIndex = (readCursor + i) & (trace_store_buffer_events - 1);

      // Copy the event while holding the lock to avoid reading a half-written event.
      thread_spinlock_lock(&b->events[eventIndex].lock);
      evt = b->events[eventIndex];
      thread_spinlock_unlock(&b->events[eventIndex].lock);

      if (evt.timeDur == 0) {
        continue; // Event is currently being recorded; skip it.
      }

      visitor(sink, userCtx, bufferIdx, tid, b->name, &evt);
    }

    thread_mutex_unlock(b->resetLock);
  }

  g_traceStoreIsVisiting = false;
}

String trace_sink_store_id(const TraceSink* sink, const u8 id) {
  diag_assert_msg(trace_sink_is_store(sink), "Given sink is not a store-sink");
  const TraceSinkStore* s = (const TraceSinkStore*)sink;
  diag_assert(id < s->idCount);
  return stringtable_lookup(g_stringtable, s->idHashes[id]);
}

TraceSink* trace_sink_store(Allocator* alloc) {
  TraceSinkStore* sink = alloc_alloc_t(alloc, TraceSinkStore);

  *sink = (TraceSinkStore){
      .api =
          {
              .eventBegin = trace_sink_store_event_begin,
              .eventEnd   = trace_sink_store_event_end,
              .destroy    = trace_sink_store_destroy,
          },
      .alloc     = alloc,
      .storeLock = thread_mutex_create(alloc),
  };

  return (TraceSink*)sink;
}

TraceSink* trace_sink_store_find(Tracer* tracer) {
  const u32 sinkCount = trace_sink_count(tracer);
  for (u32 i = 0; i != sinkCount; ++i) {
    TraceSink* sink = trace_sink(tracer, i);
    if (trace_sink_is_store(sink)) {
      return sink;
    }
  }
  return null;
}
