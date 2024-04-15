#pragma once
#include "trace_tracer.h"

struct sTraceSink {
  /**
   * Called when a event begins / ends.
   * NOTE: Functions can be invoked from different threads in parallel.
   */
  void (*eventBegin)(TraceSink*, String id, TraceColor, String msg);
  void (*eventEnd)(TraceSink*);

  /**
   * Function to call when the sink is destroyed.
   * NOTE: Can be 'null' when the sink doesn't need special destruction logic.
   */
  void (*destroy)(TraceSink*);
};
