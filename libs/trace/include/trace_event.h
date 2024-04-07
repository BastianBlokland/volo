#pragma once
#include "core_format.h"

/**
 * Output sink for trace events.
 */
typedef struct sTraceSink TraceSink;

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
#define trace_begin(_ID_, _COLOR_) trace_event_begin(string_lit(_ID_), (_COLOR_))

/**
 * Begin a new trace event with a (formatted) message payload.
 * NOTE: Must be matched with a 'trace_end()' within the same function.
 * Pre-condition: id is constant throughout the program (for example a literal).
 * Pre-condition: id only consists of ascii characters.
 * Pre-condition: msg (and format args) only consist of ascii characters.
 * Pre-condition: length of msg (after formatting) is less then 256.
 */
#define trace_begin_msg(_ID_, _COLOR_, _MSG_LIT_, ...)                                             \
  (trace_event_begin_msg(string_lit(_ID_), (_COLOR_), string_lit(_MSG_LIT_), fmt_args(__VA_ARGS__)))

/**
 * End an active trace event.
 * NOTE: Must be matched with a 'trace_begin()' within the same function.
 */
#define trace_end(void) trace_event_end()

void trace_event_add_sink(TraceSink*);
void trace_event_begin(String id, TraceColor);
void trace_event_begin_msg(String id, TraceColor, String msg, const FormatArg* args);
void trace_event_end(void);
