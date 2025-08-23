#pragma once
#include "core/format.h"

/**
 * Output sink for trace events.
 */
typedef struct sTraceSink TraceSink;

/**
 * Tracer object.
 */
typedef struct sTracer Tracer;

typedef enum eTraceColor {
  TraceColor_Default,
  TraceColor_White,
  TraceColor_Gray,
  TraceColor_Red,
  TraceColor_Green,
  TraceColor_Blue,
} TraceColor;

/**
 * Begin a new trace event.
 * NOTE: Must be matched with a 'trace_end()' within the same function.
 * Pre-condition: id is constant throughout the program (for example a literal).
 * Pre-condition: id only consists of ascii characters.
 */
#ifdef VOLO_TRACE
#define trace_begin(_ID_, _COLOR_) trace_event_begin(g_tracer, string_lit(_ID_), (_COLOR_))
#else
#define trace_begin(_ID_, _COLOR_)
#endif

/**
 * Begin a new trace event with a (formatted) message payload.
 * NOTE: Must be matched with a 'trace_end()' within the same function.
 * Pre-condition: id is constant throughout the program (for example a literal).
 * Pre-condition: id only consists of ascii characters.
 * Pre-condition: msg (and format args) only consist of ascii characters.
 * Pre-condition: length of msg (after formatting) is less then 256 bytes.
 */
#ifdef VOLO_TRACE
#define trace_begin_msg(_ID_, _COLOR_, _MSG_LIT_, ...)                                             \
  trace_event_begin_msg(                                                                           \
      g_tracer, string_lit(_ID_), (_COLOR_), string_lit(_MSG_LIT_), fmt_args(__VA_ARGS__))
#else
#define trace_begin_msg(_ID_, _COLOR_, _MSG_LIT_, ...)
#endif

/**
 * End an active trace event.
 * NOTE: Must be matched with a 'trace_begin()' within the same function.
 */
#ifdef VOLO_TRACE
#define trace_end(void) trace_event_end(g_tracer)
#else
#define trace_end(void)
#endif

/**
 * Begin a new custom (non-cpu) trace event.
 * NOTE: Must be matched with a 'trace_custom_end()'.
 * Pre-condition: stream is constant throughout the program (for example a literal).
 * Pre-condition: id is constant throughout the program (for example a literal).
 * Pre-condition: id only consists of ascii characters.
 */
#ifdef VOLO_TRACE
#define trace_custom_begin(_STREAM_, _ID_, _COLOR_)                                                \
  trace_event_custom_begin(g_tracer, string_lit(_STREAM_), string_lit(_ID_), (_COLOR_))
#else
#define trace_custom_begin(_STREAM_, _ID_, _COLOR_)
#endif

/**
 * Begin a new custom (non-cpu) trace event with a (formatted) message payload.
 * NOTE: Must be matched with a 'trace_custom_end()' within the same function.
 * Pre-condition: stream is constant throughout the program (for example a literal).
 * Pre-condition: id is constant throughout the program (for example a literal).
 * Pre-condition: id only consists of ascii characters.
 * Pre-condition: msg (and format args) only consist of ascii characters.
 * Pre-condition: length of msg (after formatting) is less then 256 bytes.
 */
#ifdef VOLO_TRACE
#define trace_custom_begin_msg(_STREAM_, _ID_, _COLOR_, _MSG_LIT_, ...)                            \
  trace_event_custom_begin_msg(                                                                    \
      g_tracer,                                                                                    \
      string_lit(_STREAM_),                                                                        \
      string_lit(_ID_),                                                                            \
      (_COLOR_),                                                                                   \
      string_lit(_MSG_LIT_),                                                                       \
      fmt_args(__VA_ARGS__))
#else
#define trace_custom_begin_msg(_STREAM_, _ID_, _COLOR_, _MSG_LIT_, ...)
#endif

/**
 * End an active trace event.
 * Pre-condition: stream is constant throughout the program (for example a literal).
 * NOTE: Must be matched with a 'trace_custom_begin()'.
 */
#ifdef VOLO_TRACE
#define trace_custom_end(_STREAM_, _TIME_, _DUR_)                                                  \
  trace_event_custom_end(g_tracer, string_lit(_STREAM_), (_TIME_), (_DUR_))
#else
#define trace_custom_end(_STREAM_, _TIME_, _DUR_)
#endif

/**
 * Global tracer.
 */
extern Tracer* g_tracer;

/**
 * Create a new tracer object.
 * Should be destroyed using 'tracer_destroy'.
 */
Tracer* trace_create(Allocator*);

/**
 * Destroy a tracer object.
 */
void trace_destroy(Tracer*);

/**
 * Add a new sink to the specified tracer object.
 * NOTE: Sinks are automatically destroyed when the tracer object is destroyed.
 */
void trace_add_sink(Tracer*, TraceSink*);

void trace_event_begin(Tracer*, String id, TraceColor);
void trace_event_begin_msg(Tracer*, String id, TraceColor, String msg, const FormatArg* args);
void trace_event_end(Tracer*);

// clang-format off

void trace_event_custom_begin(Tracer*, String stream, String id, TraceColor);
void trace_event_custom_begin_msg(Tracer*, String stream, String id, TraceColor, String msg, const FormatArg* args);
void trace_event_custom_end(Tracer*, String stream, TimeSteady time, TimeDuration dur);

// clang-format on
