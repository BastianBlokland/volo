#pragma once
#include "trace.h"

/**
 * SuperluminalSink - sink that outputs trace events to a running Superluminal profiler instance.
 * More info: https://superluminal.eu/
 */

/**
 * Create a Superluminal trace output sink.
 * NOTE: Should be registered using 'trace_event_add_sink()'.
 */
TraceSink* trace_sink_superluminal(Allocator*);
