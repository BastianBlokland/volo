#include "core_alloc.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "trace_sink_buffer.h"

#include "event_internal.h"

/**
 * Trace sink implementation that stores events in in-memory buffers to be queried later.
 */

#define trace_buffer_max_ids 16

ASSERT(trace_buffer_max_ids < u8_max, "Trace id has to be representable by a u8")

typedef struct {
  TraceSink  api;
  Allocator* alloc;

  ThreadSpinLock idLock;
  u32            idCount;
  StringHash     idHashes[trace_buffer_max_ids];

} TraceSinkBuffer;

static u8 trace_id_find(TraceSinkBuffer* b, const StringHash hash) {
  /**
   * Find a previously registered id by hash.
   * NOTE: Ids can never be removed so we don't need to take the lock but can get a false negative.
   */
  for (u8 i = 0; i != b->idCount; ++i) {
    if (b->idHashes[i] == hash) {
      return i;
    }
  }
  return sentinel_u8;
}

NO_INLINE_HINT static u8 trace_id_add(TraceSinkBuffer* b, const StringHash hash, const String str) {
  thread_spinlock_lock(&b->idLock);

  // After taking the lock check if another thread already added it, if so return that id.
  u8 result = trace_id_find(b, hash);
  if (sentinel_check(result)) {
    // Id has not been added yet; add it now.
    result = (u8)b->idCount;
    if (UNLIKELY(result == trace_buffer_max_ids)) {
      diag_crash_msg("trace: Maximum unique event ids exceeded");
    }
    b->idHashes[result] = hash;
    ++b->idCount;

    // Store the name in the global string-table so we can query for it later.
    stringtable_add(g_stringtable, str);
  }

  thread_spinlock_unlock(&b->idLock);
  return result;
}

static u8 trace_id_register(TraceSinkBuffer* b, const String str) {
  const StringHash hash   = string_hash(str);
  const u8         result = trace_id_find(b, hash);
  if (LIKELY(!sentinel_check(result))) {
    return result;
  }
  return trace_id_add(b, hash, str);
}

static void trace_sink_buffer_event_begin(
    TraceSink* sink, const String idStr, const TraceColor color, const String msg) {
  TraceSinkBuffer* buf = (TraceSinkBuffer*)sink;
  const u8         id  = trace_id_register(buf, idStr);
  (void)id;
  (void)color;
  (void)msg;
}

static void trace_sink_buffer_event_end(TraceSink* sink) {
  TraceSinkBuffer* sinkBuffer = (TraceSinkBuffer*)sink;
  (void)sinkBuffer;
}

static void trace_sink_buffer_destroy(TraceSink* sink) {
  TraceSinkBuffer* sinkBuffer = (TraceSinkBuffer*)sink;
  (void)sinkBuffer;
}

TraceSink* trace_sink_buffer(Allocator* alloc) {
  TraceSinkBuffer* sink = alloc_alloc_t(alloc, TraceSinkBuffer);

  *sink = (TraceSinkBuffer){
      .api =
          {
              .eventBegin = trace_sink_buffer_event_begin,
              .eventEnd   = trace_sink_buffer_event_end,
              .destroy    = trace_sink_buffer_destroy,
          },
      .alloc = alloc,
  };

  return (TraceSink*)sink;
}
