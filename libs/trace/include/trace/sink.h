#pragma once
#include "trace/tracer.h"

struct sTraceSink {
  /**
   * Called when a event begins / ends.
   * NOTE: Functions can be invoked from different threads in parallel.
   */
  void (*eventBegin)(TraceSink*, String id, TraceColor, String msg);
  void (*eventEnd)(TraceSink*);

  /**
   * Called when a custom (non-cpu) event begins / ends.
   * NOTE: Can be 'null' when the sink doesn't support custom events.
   * NOTE: Events for a single stream cannot be pushed in parallel, different streams can.
   */
  void (*customBegin)(TraceSink*, String stream, String id, TraceColor, String msg);
  void (*customEnd)(TraceSink*, String stream, TimeSteady time, TimeDuration dur);

  /**
   * Function to call when the sink is destroyed.
   * NOTE: Can be 'null' when the sink doesn't need special destruction logic.
   */
  void (*destroy)(TraceSink*);
};
