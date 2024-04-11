#pragma once
#include "core_annotation.h"
#include "trace_sink.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeSteady;

// Forward declare from 'core_thread.h'.
typedef i32 ThreadId;

/**
 * Store Sink - sink that outputs events to in-memory buffers for later inspection / dumping.
 * NOTE: Uses a ring-buffer per thread so events will get overwritten.
 */

typedef struct {
  ALIGNAS(64)           // Align to cacheline on x66.
  TimeSteady timeStart; // Nano-seconds since the start of the process steady clock.
  u32        timeDur;   // Duration in nano-seconds (limits the max event dur to 4 seconds).
  u8         id;        // Identifier index.
  u8         color;     // TraceColor
  u8         msgLength;
  u8         msgData[49];
} TraceStoreEvent;

ASSERT(sizeof(TraceStoreEvent) == 64, "Unexpected event size")

typedef void (*TraceStoreVisitor)(
    TraceSink*, void* userCtx, ThreadId threadId, String threadName, const TraceStoreEvent*);

/**
 * Visit all the stored events.
 * NOTE: Events are visited out of chronological order.
 */
void trace_sink_store_visit(TraceSink*, TraceStoreVisitor, void* userCtx);

/**
 * Lookup the string for the given id index
 * Pre-condition: sink to be created by 'trace_sink_store'.
 */
String trace_sink_store_id(TraceSink*, u8 id);

/**
 * Create a in-memory store trace output sink.
 * NOTE: Should be registered using 'trace_event_add_sink()'.
 */
TraceSink* trace_sink_store(Allocator*);
