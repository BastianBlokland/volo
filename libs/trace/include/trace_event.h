#pragma once
#include "core_format.h"

/**
 * Output sink for trace events.
 */
typedef struct sTraceSink TraceSink;

/**
 * Register a new trace output sink.
 * NOTE: Sinks are automatically destroyed at trace teardown.
 */
void trace_add_sink(TraceSink*);

typedef enum {
  TraceColor_Default,
  TraceColor_White,
  TraceColor_Green,
  TraceColor_Red,
} TraceColor;

/**
 * Begin a new trace event.
 * NOTE: Must be matched with a 'trace_end()' within the same function.
 * Pre-condition: id is constant throughout the program (for example a literal).
 * Pre-condition: id only consists of ascii characters.
 */
void trace_begin(String id, TraceColor);

/**
 * Begin a new trace event with a (formatted) message payload.
 * NOTE: Must be matched with a 'trace_end()' within the same function.
 * Pre-condition: id is constant throughout the program (for example a literal).
 * Pre-condition: id only consists of ascii characters.
 * Pre-condition: msg (and format args) only consist of ascii characters.
 * Pre-condition: length of msg (after formatting) is less then 256.
 */
#define trace_begin_msg(_ID_, _COLOR_, _MSG_LIT_, ...)                                             \
  (trace_begin_msg_raw((_ID_), (_COLOR_), string_lit(_MSG_LIT_), fmt_args(__VA_ARGS__)))

void trace_begin_msg_raw(String id, TraceColor, String msg, const FormatArg* args);

/**
 * End an active trace event.
 * NOTE: Must be matched with a 'trace_begin()' within the same function.
 */
void trace_end();
