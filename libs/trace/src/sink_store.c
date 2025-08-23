#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/math.h"
#include "core/stringtable.h"
#include "core/thread.h"
#include "core/time.h"
#include "trace/sink.h"
#include "trace/sink_store.h"

#include "tracer.h"

#ifdef VOLO_SIMD
#include "core/simd.h"
#endif

/**
 * Trace sink implementation that stores events in in-memory buffers to be queried later.
 */

#define trace_store_max_ids 64
#define trace_store_max_buffers 16
#define trace_store_buffer_events 1024
#define trace_store_buffer_max_depth 8

ASSERT(trace_store_max_ids < u8_max, "Trace id has to be representable by a u8");
ASSERT((trace_store_buffer_events & (trace_store_buffer_events - 1u)) == 0, "Has to be a pow2");
ASSERT(trace_store_buffer_events < u16_max, "Events have to be representable with a u16");

static THREAD_LOCAL bool g_traceStoreIsVisiting;

typedef enum {
  TraceBufferType_Thread,
  TraceBufferType_Custom,
} TraceBufferType;

typedef struct {
  String          streamName;
  i32             streamId;
  TraceBufferType type;

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
  i32         streamCounter;

  ALIGNAS(16)
  StringHash idHashes[trace_store_max_ids];
  u32        idCount;

  u32          bufferCount;
  ThreadId     bufferThreadIds[trace_store_max_buffers];
  TraceBuffer* buffers[trace_store_max_buffers];

} TraceSinkStore;

static u8 trace_id_find(TraceSinkStore* s, const StringHash hash) {
#ifdef VOLO_SIMD
  ASSERT((trace_store_max_ids % 8) == 0, "Only multiple of 8 id counts are supported");

  const SimdVec hashVec = simd_vec_broadcast_u32(hash);
  for (u32 i = 0; i != trace_store_max_ids; i += 8) {
    const SimdVec eqA    = simd_vec_eq_u32(simd_vec_load(s->idHashes + i), hashVec);
    const SimdVec eqB    = simd_vec_eq_u32(simd_vec_load(s->idHashes + i + 4), hashVec);
    const u32     eqMask = simd_vec_mask_u8(simd_vec_pack_u32_to_u16(eqA, eqB));

    if (eqMask) {
      return i + intrinsic_ctz_32(eqMask) / 2; // Div 2 due to 16 bit entries.
    }
  }
  return sentinel_u8;
#else
  for (u8 i = 0; i != s->idCount; ++i) {
    if (s->idHashes[i] == hash) {
      return i;
    }
  }
  return sentinel_u8;
#endif
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

NO_INLINE_HINT static TraceBuffer* trace_buffer_add(
    TraceSinkStore* s, const TraceBufferType type, const ThreadId tid, const String name) {
  thread_mutex_lock(s->storeLock);
  TraceBuffer* result = null;

  // Check if there's a buffer for a thread that has exited, if so we can re-use its buffer.
  for (u32 i = 0; i != s->bufferCount; ++i) {
    if (s->buffers[i]->type == TraceBufferType_Thread && !thread_exists(s->bufferThreadIds[i])) {
      /**
       * TODO: The nested locks are not very elegant (and can stall all events while a potential
       * slow visit is happening), at the moment we assume that starting / stopping threads is rare.
       */
      thread_mutex_lock(s->buffers[i]->resetLock);
      {
        s->bufferThreadIds[i] = type == TraceBufferType_Thread ? tid : 0;
        result                = s->buffers[i];

        diag_assert(result->stackCount == 0);
        string_maybe_free(s->alloc, result->streamName);

        result->type        = type;
        result->streamId    = s->streamCounter++;
        result->streamName  = string_maybe_dup(s->alloc, name);
        result->eventCursor = 0;
        mem_set(array_mem(result->events), 0);
      }
      thread_mutex_unlock(s->buffers[i]->resetLock);
      break;
    }
  }

  // If no buffer that can be re-used was found, then create a new buffer.
  if (!result) {
    if (UNLIKELY(s->bufferCount == trace_store_max_buffers)) {
      diag_crash_msg("trace: Maximum stream-count exceeded");
    }
    result              = alloc_alloc_t(s->alloc, TraceBuffer);
    result->type        = type;
    result->streamId    = s->streamCounter++;
    result->streamName  = string_maybe_dup(s->alloc, name);
    result->resetLock   = thread_mutex_create(s->alloc);
    result->eventCursor = result->stackCount = 0;
    mem_set(array_mem(result->events), 0);

    s->bufferThreadIds[s->bufferCount] = type == TraceBufferType_Thread ? tid : 0;
    s->buffers[s->bufferCount]         = result;
    ++s->bufferCount;
  }

  thread_mutex_unlock(s->storeLock);
  return result;
}

INLINE_HINT static TraceBuffer* trace_thread_find(TraceSinkStore* s, const ThreadId tid) {
  for (u32 i = 0; i != s->bufferCount; ++i) {
    if (s->bufferThreadIds[i] == tid) {
      return s->buffers[i];
    }
  }
  return null;
}

INLINE_HINT static TraceBuffer* trace_thread_register(TraceSinkStore* s, const ThreadId tid) {
  TraceBuffer* result = trace_thread_find(s, tid);
  if (LIKELY(result)) {
    return result;
  }
  return trace_buffer_add(s, TraceBufferType_Thread, tid, g_threadName);
}

static TraceBuffer* trace_custom_find(TraceSinkStore* s, const String name) {
  for (u32 i = 0; i != s->bufferCount; ++i) {
    if (s->buffers[i]->type != TraceBufferType_Custom) {
      continue; // Not a custom buffer.
    }
    if (string_eq(s->buffers[i]->streamName, name)) {
      return s->buffers[i];
    }
  }
  return null;
}

static TraceBuffer* trace_custom_register(TraceSinkStore* s, const String name) {
  TraceBuffer* result = trace_custom_find(s, name);
  if (LIKELY(result)) {
    return result;
  }
  return trace_buffer_add(s, TraceBufferType_Custom, 0 /* tid */, name);
}

INLINE_HINT static void trace_buffer_advance(TraceBuffer* b) {
  b->eventCursor = (b->eventCursor + 1) & (trace_store_buffer_events - 1);
}

INLINE_HINT static void trace_buffer_begin(
    TraceBuffer*     b,
    const u8         id,
    const TraceColor color,
    const String     msg,
    const TimeSteady timeStart) {
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
    evt->timeDur    = 0;
    evt->timeStart  = timeStart;
    evt->id         = id;
    evt->stackDepth = b->stackCount;
    evt->color      = (u8)color;
    evt->msgLength  = (u8)math_min(msg.size, array_elems(evt->msgData));
    mem_cpy(array_mem(evt->msgData), mem_create(msg.ptr, evt->msgLength));
  }
  thread_spinlock_unlock(&evt->lock);

  if (UNLIKELY(b->stackCount >= trace_store_buffer_max_depth)) {
    diag_crash_msg("trace: Trace event exceeded the maximum stack depth");
  }
  // Add it to the stack.
  b->stack[b->stackCount++] = b->eventCursor;

  // Advance the cursor.
  trace_buffer_advance(b);
}

static void trace_sink_store_event_begin(
    TraceSink* sink, const String id, const TraceColor color, const String msg) {
  TraceSinkStore* s = (TraceSinkStore*)sink;
  TraceBuffer*    b = trace_thread_register(s, g_threadTid);

  trace_buffer_begin(b, trace_id_register(s, id), color, msg, time_steady_clock());
}

static void trace_sink_store_event_end(TraceSink* sink) {
  TraceSinkStore* s = (TraceSinkStore*)sink;
  TraceBuffer*    b = trace_thread_find(s, g_threadTid);
  diag_assert_msg(b && b->stackCount, "trace: Event ended that never started on this thread");

  // Pop the top-most event from the stack.
  TraceStoreEvent* evt = &b->events[b->stack[--b->stackCount]];
  if (UNLIKELY(evt->timeDur)) {
    /**
     * Event has already ended.
     * NOTE: This can happen for very long-running events where the task-id was reused before the
     * event ended. Here we have no choice but to drop the event, if this often happens then the
     * buffer size should be increased.
     */
    return;
  }

  // Compute the event time.
  const TimeDuration dur = time_steady_duration(evt->timeStart, time_steady_clock());

  /**
   * NOTE: If the platforms timer granularity is imprecise then the duration can actually be
   * reported as 0 nano-seconds, to avoid this we make sure its always at least 1 ns.
   */
  evt->timeDur = dur > u32_max ? u32_max : math_max((u32)dur, 1);
}

static void trace_sink_store_custom_begin(
    TraceSink*       sink,
    const String     stream,
    const String     id,
    const TraceColor color,
    const String     msg) {
  TraceSinkStore* s = (TraceSinkStore*)sink;
  TraceBuffer*    b = trace_custom_register(s, stream);

  trace_buffer_begin(b, trace_id_register(s, id), color, msg, 0);
}

static void trace_sink_store_custom_end(
    TraceSink* sink, const String stream, const TimeSteady time, const TimeDuration dur) {
  TraceSinkStore* s = (TraceSinkStore*)sink;
  TraceBuffer*    b = trace_custom_find(s, stream);
  diag_assert_msg(b && b->stackCount, "trace: Custom event ended that never started on the stream");

  // Pop the top-most event from the stack.
  TraceStoreEvent* evt = &b->events[b->stack[--b->stackCount]];
  diag_assert_msg(!evt->timeDur, "trace: Event ended twice");

  evt->timeStart = time;
  evt->timeDur   = math_max((u32)dur, 1);
}

static void trace_sink_store_destroy(TraceSink* sink) {
  TraceSinkStore* s = (TraceSinkStore*)sink;
  for (u32 i = 0; i != s->bufferCount; ++i) {
    diag_assert(!s->buffers[i]->stackCount);
    string_maybe_free(s->alloc, s->buffers[i]->streamName);
    thread_mutex_destroy(s->buffers[i]->resetLock);
    alloc_free_t(s->alloc, s->buffers[i]);
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

  if (UNLIKELY(g_traceStoreIsVisiting)) {
    diag_crash_msg("trace: Unable to perform nested visits");
  }

  g_traceStoreIsVisiting = true;

  TraceStoreEvent evt;

  for (u32 bufferIdx = 0; bufferIdx != s->bufferCount; ++bufferIdx) {
    TraceBuffer* b = s->buffers[bufferIdx];
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

      visitor(sink, userCtx, b->streamId, b->streamName, &evt);
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
              .eventBegin  = trace_sink_store_event_begin,
              .eventEnd    = trace_sink_store_event_end,
              .customBegin = trace_sink_store_custom_begin,
              .customEnd   = trace_sink_store_custom_end,
              .destroy     = trace_sink_store_destroy,
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
