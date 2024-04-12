#pragma once
#include "trace_tracer.h"

void trace_global_tracer_init(void);
void trace_global_tracer_teardown(void);

u32        trace_sink_count(const Tracer*);
TraceSink* trace_sink(Tracer*, u32 index); // Pointer is valid until tracer destruction.
