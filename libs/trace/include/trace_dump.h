#pragma once
#include "trace_tracer.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

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
 * NOTE: 'storeSink' has to be created from the 'trace_sink_store()' api.
 */
void trace_dump_eventtrace(TraceSink* storeSink, DynString* out);
bool trace_dump_eventtrace_to_path(TraceSink* storeSink, String path);
