#pragma once
#include "trace_tracer.h"

/**
 * Dump all the events from the store in the Google EventTrace format.
 *
 * The Google EventTrace format can viewed in various viewers:
 * - Chromium based browsers: 'chrome://tracing/'
 * - Perfetto: https://ui.perfetto.dev/
 * - Catapult's trace2html (https://chromium.googlesource.com/catapult)
 *      $CATAPULT/tracing/bin/trace2html my_trace.json --output=my_trace.html
 *
 * Spec: https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/edit
 *
 * NOTE: The storeSink uses a ring-buffer per thread, meaning that threads with allot of activity
 * will exhaust their ring-buffer faster then threads with little activity. The result of this is
 * that the trail of the data might look odd as some threads will have data while others wont.
 *
 * NOTE: 'storeSink' has to be created from the 'trace_sink_store()' api.
 */
void trace_dump_eventtrace(const TraceSink* storeSink, DynString* out);
bool trace_dump_eventtrace_to_path(const TraceSink* storeSink, String path);
bool trace_dump_eventtrace_to_path_default(const TraceSink* storeSink);
