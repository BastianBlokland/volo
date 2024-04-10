#pragma once
#include "core_annotation.h"
#include "trace_sink.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeSteady;

/**
 * Store Sink - sink that outputs events to in-memory buffers for later inspection / dumping.
 */

typedef struct {
  TimeSteady timeStart; // Nano-seconds since the start of the process steady clock.
  u32        timeDur;   // Duration in nano-seconds (limits the max event dur to 4 seconds).
  u8         id;        // Identifier label index.
  u8         color;     // TraceColor
  u8         msgLength;
  u8         msgData[49];
} TraceStoreEvent;

ASSERT(sizeof(TraceStoreEvent) == 64, "Unexpected event size")

/**
 * Create a in-memory store trace output sink.
 * NOTE: Should be registered using 'trace_event_add_sink()'.
 */
TraceSink* trace_sink_store(Allocator*);
