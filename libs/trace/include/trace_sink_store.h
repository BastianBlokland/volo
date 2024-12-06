#pragma once
#include "core.h"
#include "trace_tracer.h"

/**
 * Store Sink - sink that outputs events to in-memory buffers for later inspection / dumping.
 *
 * NOTE: Uses a ring-buffer per thread, meaning that threads with allot of activity will exhaust
 * their ring-buffer faster then threads with little activity. The result of this is
 * that the trail of the data might look odd as some threads will have data while others wont.
 */

typedef struct sTraceStoreEvent {
  ALIGNAS(64) // Align to cacheline on x66.
  ThreadSpinLock lock;
  u32            timeDur;    // Duration in nano-seconds (limits the max event dur to 4 seconds).
  TimeSteady     timeStart;  // Nano-seconds since the start of the process steady clock.
  u8             id;         // Identifier index.
  u8             stackDepth; // Depth of the trace stack (amount of parent events).
  u8             color;      // TraceColor
  u8             msgLength;
  u8             msgData[44];
} TraceStoreEvent;

ASSERT(sizeof(TraceStoreEvent) == 64, "Unexpected event size")

typedef void (*TraceStoreVisitor)(
    const TraceSink*,
    void*    userCtx,
    u32      bufferIdx,
    ThreadId threadId,
    String   threadName,
    const TraceStoreEvent*);

/**
 * Visit all the stored events.
 * NOTE: Events are visited out of chronological order.
 * NOTE: Make sure that the callback is fast as we can potentially stall events while visiting.
 */
void trace_sink_store_visit(const TraceSink*, TraceStoreVisitor, void* userCtx);

/**
 * Lookup the string for the given id index
 * Pre-condition: sink to be created by 'trace_sink_store'.
 */
String trace_sink_store_id(const TraceSink*, u8 id);

/**
 * Create a in-memory store trace output sink.
 * NOTE: Should be registered using 'trace_event_add_sink()'.
 */
TraceSink* trace_sink_store(Allocator*);

/**
 * Find an existing store-sink that is registered to the given tracer.
 * Returns null if no store-sink was found.
 * NOTE: Pointer is valid until the tracer is destroyed.
 */
TraceSink* trace_sink_store_find(Tracer*);
