#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/dynstring.h"
#include "core/float.h"
#include "core/format.h"
#include "core/math.h"
#include "core/sort.h"
#include "dev/panel.h"
#include "dev/register.h"
#include "dev/trace.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "jobs/forward.h"
#include "trace/dump.h"
#include "trace/sink_store.h"
#include "trace/tracer.h"
#include "ui/canvas.h"
#include "ui/layout.h"
#include "ui/panel.h"
#include "ui/scrollview.h"
#include "ui/shape.h"
#include "ui/style.h"
#include "ui/table.h"
#include "ui/widget.h"

// clang-format off

static const String g_tooltipFreeze       = string_static("Freeze the data set (halts data collection).");
static const String g_tooltipRefresh      = string_static("Refresh the data set.");
static const String g_tooltipTrigger      = string_static("Configure the trigger (auto freeze) settings.");
static const String g_tooltipTriggerPick  = string_static("Trigger on '{}' event.");
static const String g_tooltipTraceDump    = string_static("Dump performance trace data to disk (in the 'logs' directory).");
static const String g_messageNoStoreSink  = string_static("No store trace-sink found.\nNote: Check if the binary was compiled with the 'TRACE' option and not explicitly disabled.");

// clang-format on

#define dev_trace_max_name_length 15
#define dev_trace_max_streams 16
#define dev_trace_default_depth 3

typedef struct {
  i32      id;
  u8       nameLength;
  u8       nameBuffer[dev_trace_max_name_length];
  DynArray events; // TraceStoreEvent[]
} DevTraceData;

typedef struct {
  bool         enabled, picking;
  u8           eventId;
  DynString    msgFilter;
  TimeDuration threshold;
} DevTraceTrigger;

ecs_comp_define(DevTracePanelComp) {
  UiPanel         panel;
  UiScrollview    scrollview;
  bool            freeze, refresh;
  bool            hoverAny, panAny;
  u32             eventDepth;
  TimeSteady      timeHead;
  TimeDuration    timeWindow;
  DevTraceTrigger trigger;
  DevTraceData*   streams;                              // DevTraceData[dev_trace_max_streams].
  u8              streamSorting[dev_trace_max_streams]; // streamIdx[].
};

static void ecs_destruct_trace_panel(void* data) {
  DevTracePanelComp* comp = data;
  dynstring_destroy(&comp->trigger.msgFilter);

  for (u32 streamIdx = 0; streamIdx != dev_trace_max_streams; ++streamIdx) {
    DevTraceData* streamData = &comp->streams[streamIdx];
    dynarray_destroy(&streamData->events);
  }
  alloc_free_array_t(g_allocHeap, comp->streams, dev_trace_max_streams);
}

static void trace_trigger_set(DevTraceTrigger* t, const u8 eventId) {
  t->eventId = eventId;
  t->enabled = true;
  t->picking = false;
}

static bool trace_trigger_match(const DevTraceTrigger* t, const TraceStoreEvent* evt) {
  if (!t->enabled) {
    return false;
  }
  if (evt->id != t->eventId) {
    return false;
  }
  if (evt->timeDur < t->threshold) {
    return false;
  }
  if (!t->msgFilter.size) {
    return true;
  }
  const String evtMsg = mem_create(evt->msgData, evt->msgLength);
  return string_match_glob(evtMsg, dynstring_view(&t->msgFilter), StringMatchFlags_IgnoreCase);
}

static UiColor trace_event_color(const TraceColor col) {
  switch (col) {
  case TraceColor_Default:
  case TraceColor_White:
    return ui_color(178, 178, 178, 178);
  case TraceColor_Gray:
    return ui_color(64, 64, 64, 178);
  case TraceColor_Red:
    return ui_color(255, 16, 16, 178);
  case TraceColor_Green:
    return ui_color(16, 128, 16, 178);
  case TraceColor_Blue:
    return ui_color(16, 16, 255, 178);
  }
  diag_crash_msg("Unsupported trace color");
}

static void trace_data_clear(DevTracePanelComp* panel) {
  for (u32 streamIdx = 0; streamIdx != dev_trace_max_streams; ++streamIdx) {
    DevTraceData* streamData = &panel->streams[streamIdx];
    streamData->id           = -1;
    streamData->nameLength   = 0;
    dynarray_clear(&streamData->events);
  }
}

static void trace_data_focus(DevTracePanelComp* panel, const TraceStoreEvent* evt) {
  panel->timeHead   = evt->timeStart + evt->timeDur;
  panel->timeWindow = math_clamp_i64(evt->timeDur, time_microsecond, time_milliseconds(500));
  panel->freeze     = true;
}

NO_INLINE_HINT static DevTraceData*
trace_data_stream_register(DevTracePanelComp* panel, const i32 id, const String name) {
  diag_assert(id >= 0);

  for (u32 streamIdx = 0; streamIdx != dev_trace_max_streams; ++streamIdx) {
    DevTraceData* streamData = &panel->streams[streamIdx];
    if (streamData->id < 0) {
      streamData->id         = id;
      streamData->nameLength = (u8)math_min(name.size, dev_trace_max_name_length);
      mem_cpy(array_mem(streamData->nameBuffer), mem_slice(name, 0, streamData->nameLength));
      return streamData;
    }
  }
  diag_crash_msg("dev: Trace stream count exceeds maximum");
}

static DevTraceData* trace_data_get(DevTracePanelComp* panel, const i32 id, const String name) {
  diag_assert(id >= 0);

  for (u32 streamIdx = 0; streamIdx != dev_trace_max_streams; ++streamIdx) {
    DevTraceData* streamData = &panel->streams[streamIdx];
    if (streamData->id == id) {
      return streamData;
    }
  }
  return trace_data_stream_register(panel, id, name);
}

static void trace_data_visitor(
    const TraceSink*       sink,
    void*                  userCtx,
    const i32              streamId,
    const String           streamName,
    const TraceStoreEvent* evt) {
  (void)sink;

  DevTracePanelComp* panel      = userCtx;
  DevTraceData*      streamData = trace_data_get(panel, streamId, streamName);
  *((TraceStoreEvent*)dynarray_push(&streamData->events, 1).ptr) = *evt;

  if (UNLIKELY(trace_trigger_match(&panel->trigger, evt))) {
    trace_data_focus(panel, evt);
  }
}

/**
 * HACK: We currently have no way to pass a context to a compare function, so we temporarily store
 * it in a thread-local global variable.
 */
static THREAD_LOCAL const DevTraceData* g_devTraceSortStreams;

static i8 trace_stream_compare(const void* a, const void* b) {
  diag_assert(g_devTraceSortStreams);
  const DevTraceData* streamA = &g_devTraceSortStreams[*((const u8*)a)];
  const DevTraceData* streamB = &g_devTraceSortStreams[*((const u8*)b)];

  if (streamA->id < 0 || streamB->id < 0) {
    return compare_i32(&streamA->id, &streamB->id);
  }

  const String streamNameA = mem_create(streamA->nameBuffer, streamA->nameLength);
  const String streamNameB = mem_create(streamB->nameBuffer, streamB->nameLength);

  return compare_string_reverse(&streamNameA, &streamNameB);
}

static void trace_stream_sort(DevTracePanelComp* panel) {
  // Initialize the sorting to identity.
  for (u32 streamIdx = 0; streamIdx != dev_trace_max_streams; ++streamIdx) {
    diag_assert(streamIdx < u8_max);
    panel->streamSorting[streamIdx] = (u8)streamIdx;
  }

  // Sort the stream indices based on their name.
  g_devTraceSortStreams = panel->streams;
  sort_quicksort_t(
      panel->streamSorting, panel->streamSorting + dev_trace_max_streams, u8, trace_stream_compare);
  g_devTraceSortStreams = null;
}

static UiColor trace_trigger_button_color(const DevTraceTrigger* t) {
  UiColor color = ui_color(32, 32, 32, 192);
  if (t->picking) {
    color = ui_color(255, 16, 0, 192);
  } else if (t->enabled) {
    color = ui_color(16, 192, 0, 192);
  }
  return color;
}

static void
trace_options_trigger_draw(UiCanvasComp* c, DevTracePanelComp* panel, const TraceSink* sinkStore) {
  static const UiVector g_popupSize = {.x = 255.0f, .y = 130.0f};

  DevTraceTrigger*        t           = &panel->trigger;
  const UiId              popupId     = ui_canvas_id_peek(c);
  const UiPersistentFlags popupFlags  = ui_canvas_persistent_flags(c, popupId);
  const bool              popupActive = (popupFlags & UiPersistentFlags_Open) != 0;
  const bool              hasEvent    = !sentinel_check(t->eventId);

  const String  trigLabel = string_lit("Trigger");
  const UiColor trigColor = trace_trigger_button_color(&panel->trigger);
  if (ui_button(c, .label = trigLabel, .tooltip = g_tooltipTrigger, .frameColor = trigColor)) {
    ui_canvas_persistent_flags_toggle(c, popupId, UiPersistentFlags_Open);
  }

  ui_canvas_id_block_next(c); // Put the popup on its own id-block.

  ui_style_push(c);
  if (popupActive) {
    ui_style_layer(c, UiLayer_Popup);
    ui_canvas_min_interact_layer(c, UiLayer_Popup);

    ui_layout_push(c);
    ui_layout_move(c, ui_vector(0.5f, 0.5f), UiBase_Current, Ui_XY);
    ui_layout_resize(c, UiAlign_TopCenter, g_popupSize, UiBase_Absolute, Ui_XY);

    // Popup background.
    ui_style_push(c);
    ui_style_outline(c, 2);
    ui_style_color(c, ui_color(64, 64, 64, 235));
    ui_canvas_draw_glyph(c, UiShape_Circle, 5, UiFlags_Interactable);
    ui_style_pop(c);

    // Popup content.
    ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

    UiTable table = ui_table();
    ui_table_add_column(&table, UiTableColumn_Fixed, 90);
    ui_table_add_column(&table, UiTableColumn_Fixed, 150);

    ui_table_next_row(c, &table);
    ui_label(c, string_lit("Action"));
    ui_table_next_column(c, &table);
    const UiColor enabledColor = t->enabled ? ui_color(16, 192, 0, 192) : ui_color(255, 16, 0, 192);
    if (t->enabled) {
      if (ui_button(c, .label = string_lit("Disable"), .frameColor = enabledColor)) {
        t->enabled = false;
      }
    } else {
      const UiWidgetFlags enableFlags = hasEvent ? UiWidget_Default : UiWidget_Disabled;
      if (ui_button(
              c, .label = string_lit("Enable"), .flags = enableFlags, .frameColor = enabledColor)) {
        t->enabled    = true;
        panel->freeze = false;
      }
    }

    ui_table_next_row(c, &table);
    ui_label(c, string_lit("Event"));
    ui_table_next_column(c, &table);
    if (ui_button(
            c,
            .label = hasEvent ? trace_sink_store_id(sinkStore, t->eventId) : string_lit("Pick"),
            .frameColor = hasEvent ? ui_color(16, 192, 0, 192) : ui_color(255, 16, 0, 192))) {
      t->picking    = true;
      t->enabled    = false;
      panel->freeze = true;
      ui_canvas_persistent_flags_unset(c, popupId, UiPersistentFlags_Open);
    }

    ui_table_next_row(c, &table);
    ui_label(c, string_lit("Message"));
    ui_table_next_column(c, &table);
    if (ui_textbox(c, &t->msgFilter, .placeholder = string_lit("*"))) {
      if (t->enabled) {
        panel->freeze = false;
      }
    }

    ui_table_next_row(c, &table);
    ui_label(c, string_lit("Threshold"));
    ui_table_next_column(c, &table);
    if (ui_durbox(c, &t->threshold, .min = time_microsecond, .max = time_milliseconds(500))) {
      if (t->enabled) {
        panel->freeze = false;
      }
    }

    ui_layout_container_pop(c);
    ui_layout_pop(c);

    // Close popup when pressing outside.
    if (ui_canvas_input_any(c) && ui_canvas_group_block_inactive(c)) {
      ui_canvas_persistent_flags_unset(c, popupId, UiPersistentFlags_Open);
    }
  }
  ui_style_pop(c);

  ui_canvas_id_block_next(c); // End on an consistent id.
}

static void
trace_options_draw(UiCanvasComp* c, DevTracePanelComp* panel, const TraceSink* sinkStore) {
  ui_layout_push(c);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 160);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 40);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);

  ui_table_next_row(c, &table);

  if (panel->trigger.picking) {
    ui_label(c, string_lit("Picking event"));
  } else {
    const String timeLabel = fmt_write_scratch(
        "Window: {}",
        fmt_duration(panel->timeWindow, .minIntDigits = 3, .minDecDigits = 1, .maxDecDigits = 1));
    ui_label(c, timeLabel);
  }

  ui_table_next_column(c, &table);
  ui_label(c, string_lit("Depth:"));
  ui_table_next_column(c, &table);
  f64 depthVal = panel->eventDepth;
  if (ui_numbox(c, &depthVal, .min = 1, .max = 8, .step = 1)) {
    panel->eventDepth = (u32)depthVal;
  }

  ui_table_next_column(c, &table);
  ui_label(c, string_lit("Freeze:"));
  ui_table_next_column(c, &table);
  const UiWidgetFlags freezeFlags = panel->trigger.picking ? UiWidget_Disabled : UiWidget_Default;
  ui_toggle(c, &panel->freeze, .tooltip = g_tooltipFreeze, .flags = freezeFlags);

  ui_table_next_column(c, &table);
  const String        refreshLabel   = string_lit("Refresh");
  const bool          refreshBlocked = !panel->freeze || panel->trigger.picking;
  const UiWidgetFlags refreshFlags   = refreshBlocked ? UiWidget_Disabled : UiWidget_Default;
  if (ui_button(c, .label = refreshLabel, .tooltip = g_tooltipRefresh, .flags = refreshFlags)) {
    panel->refresh = true;
  }

  ui_table_next_column(c, &table);
  trace_options_trigger_draw(c, panel, sinkStore);

  ui_table_next_column(c, &table);
  if (ui_button(c, .label = string_lit("Dump"), .tooltip = g_tooltipTraceDump)) {
    trace_dump_eventtrace_to_path_default(sinkStore);
  }

  ui_layout_pop(c);
}

static void trace_data_input_zoom(UiCanvasComp* c, DevTracePanelComp* panel, const UiRect rect) {
  const f64 zoomSpeed = 0.1;
  const f64 zoomFrac  = 1.0 - ui_canvas_input_scroll(c).y * zoomSpeed;

  const TimeDuration min = time_microsecond;
  const TimeDuration max = time_milliseconds(500);
  const TimeDuration new = math_clamp_i64((i64)((f64)panel->timeWindow * zoomFrac), min, max);

  const TimeDuration diff = new - panel->timeWindow;
  if (panel->freeze && rect.width > f32_epsilon) {
    // Zoom from the cursor's position when frozen.
    const f64 pivot = (ui_canvas_input_pos(c).x - rect.x) / rect.width;
    panel->timeHead += (TimeDuration)((f64)diff * (1.0 - pivot));
  }
  panel->timeWindow = new;
}

static void trace_data_input_pan(UiCanvasComp* c, DevTracePanelComp* panel, const UiRect rect) {
  if (rect.width > f32_epsilon) {
    const f64 inputFrac = ui_canvas_input_delta(c).x / rect.width;
    panel->timeHead -= (TimeDuration)((f64)panel->timeWindow * inputFrac);
  }
}

static void trace_data_tooltip_draw(
    UiCanvasComp*          c,
    const UiId             barId,
    const TraceStoreEvent* evt,
    const String           msg,
    const String           id) {
  DynString tooltipBuffer = dynstring_create_over(mem_stack(256));
  if (msg.size) {
    fmt_write(&tooltipBuffer, "\a.bMessage\ar:\a>12{}\n", fmt_text(msg));
  }
  fmt_write(&tooltipBuffer, "\a.bId\ar:\a>12{}\n", fmt_text(id));
  fmt_write(&tooltipBuffer, "\a.bDuration\ar:\a>12{}\n", fmt_duration(evt->timeDur));
  ui_tooltip(c, barId, dynstring_view(&tooltipBuffer));
}

static void trace_data_ruler_draw(UiCanvasComp* c, const f32 x, const UiRect bgRect) {
  ui_style_push(c);
  ui_style_color(c, ui_color(255, 255, 255, 128));
  ui_style_outline(c, 0);
  const UiVector from = ui_vector(x, bgRect.y);
  const UiVector to   = ui_vector(x, bgRect.y + bgRect.height);
  ui_line(c, from, to, .base = UiBase_Absolute, .width = 1.0f);
  ui_style_pop(c);
}

static void trace_data_events_draw(
    UiCanvasComp*       c,
    DevTracePanelComp*  panel,
    const DevTraceData* data,
    const TraceSink*    sinkStore) {
  ui_layout_push(c);
  ui_layout_container_push(c, UiClip_None, UiLayer_Normal);
  ui_style_push(c);

  ui_canvas_id_block_next(c); // Start events on their own id-block.

  // Draw an invisible elem as background zoom / pan target.
  const UiFlags bgFlags = UiFlags_Interactable | UiFlags_TrackRect;
  const UiId    bgId    = ui_canvas_draw_glyph(c, UiShape_Empty, 0, bgFlags);
  const UiRect  bgRect  = ui_canvas_elem_rect(c, bgId);

  // Zoom and pan input.
  const UiStatus blockStatus = ui_canvas_group_block_status(c);
  if (blockStatus == UiStatus_Hovered) {
    panel->hoverAny = true;
    trace_data_input_zoom(c, panel, bgRect);
  }
  if (panel->freeze && blockStatus >= UiStatus_Pressed) {
    static const f32 g_panThreshold = 1.5f;
    if (panel->panAny || math_abs(ui_canvas_input_delta(c).x) > g_panThreshold) {
      panel->panAny = true;
      trace_data_input_pan(c, panel, bgRect);
    }
  }

  // NOTE: Timestamps are in nanoseconds.
  const f64 timeLeft  = (f64)(panel->timeHead - panel->timeWindow);
  const f64 timeRight = (f64)panel->timeHead;

  const f32 eventHeight = 1.0f / panel->eventDepth;
  dynarray_for_t(&data->events, TraceStoreEvent, evt) {
    const f64 fracLeft  = math_unlerp(timeLeft, timeRight, (f64)evt->timeStart);
    const f64 fracRight = math_unlerp(timeLeft, timeRight, (f64)(evt->timeStart + evt->timeDur));

    if (fracRight <= 0.0 || fracLeft >= 1.0 || evt->stackDepth >= panel->eventDepth) {
      ui_canvas_id_skip(c, 4); // 4: +1 for bar, +1 for label, +2 for tooltip.
      continue;                // Event outside of the visible region.
    }
    const f64 fracLeftClamped  = math_max(fracLeft, 0.0);
    const f64 fracRightClamped = math_min(fracRight, 1.0);

    const f64      fracWidth = fracRightClamped - fracLeftClamped;
    const UiVector size      = {.width = (f32)fracWidth, .height = eventHeight};
    const UiVector pos       = {
              .x = (f32)fracLeftClamped,
              .y = 1.0f - size.height * (evt->stackDepth + 1),
    };
    ui_layout_set(c, ui_rect(pos, size), UiBase_Container);

    const UiId     barId      = ui_canvas_id_peek(c);
    const UiStatus barStatus  = ui_canvas_elem_status(c, barId);
    const bool     barHovered = barStatus >= UiStatus_Hovered;

    ui_style_outline(c, barHovered ? 2 : 1);
    ui_style_color_with_mult(c, trace_event_color(evt->color), barHovered ? 2.0f : 1.0f);
    ui_canvas_draw_glyph(c, UiShape_Square, 5, UiFlags_Interactable);

    if (barHovered && panel->freeze) {
      const String id  = trace_sink_store_id(sinkStore, evt->id);
      const String msg = mem_create(evt->msgData, evt->msgLength);

      ui_canvas_interact_type(c, UiInteractType_Action);
      if (!panel->panAny && barStatus == UiStatus_Activated) {
        ui_canvas_sound(c, UiSoundType_Click);
        if (panel->trigger.picking) {
          trace_trigger_set(&panel->trigger, evt->id);
          panel->freeze = false;
        } else {
          trace_data_focus(panel, evt);
        }
      }
      if (panel->trigger.picking) {
        ui_tooltip(
            c, barId, format_write_formatted_scratch(g_tooltipTriggerPick, fmt_args(fmt_text(id))));
      } else {
        trace_data_tooltip_draw(c, barId, evt, msg, id);
      }
    } else {
      ui_canvas_id_skip(c, 2); // NOTE: Tooltips consume two ids.
    }

    static const f32 g_minWidthForLabel = 100.0f;
    if (fracWidth * bgRect.width > g_minWidthForLabel) {
      const String id  = trace_sink_store_id(sinkStore, evt->id);
      const String msg = mem_create(evt->msgData, evt->msgLength);

      ui_style_outline(c, 1);
      ui_style_color(c, ui_color_white);
      ui_canvas_draw_text(c, msg.size ? msg : id, 12, UiAlign_MiddleCenter, UiFlags_None);
    } else {
      ui_canvas_id_skip(c, 1);
    }
  }
  ui_canvas_id_block_next(c); // End on an consistent id in case of varying event counts.

  const f32 inputX = ui_canvas_input_pos(c).x;
  if (panel->hoverAny && inputX > bgRect.x && inputX < (bgRect.x + bgRect.width)) {
    trace_data_ruler_draw(c, inputX, bgRect);
  } else {
    ui_canvas_id_skip(c, 1);
  }

  ui_style_pop(c);
  ui_layout_container_pop(c);
  ui_layout_pop(c);
}

static void
trace_panel_draw(UiCanvasComp* c, DevTracePanelComp* panel, const TraceSink* sinkStore) {
  const String title = fmt_write_scratch("{} Trace Panel", fmt_ui_shape(QueryStats));
  ui_panel_begin(c, &panel->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  if (sinkStore) {
    trace_options_draw(c, panel, sinkStore);
    ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
    ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

    static const UiVector g_tablePadding = {10, 5};
    UiTable table = ui_table(.spacing = g_tablePadding, .rowHeight = 20 * panel->eventDepth);
    ui_table_add_column(&table, UiTableColumn_Fixed, 125);
    ui_table_add_column(&table, UiTableColumn_Flexible, 0);

    ui_table_draw_header(
        c,
        &table,
        (const UiTableColumnName[]){
            {string_lit("Stream"), string_lit("Name of the stream.")},
            {string_lit("Events"), string_lit("Traced events on the stream.")},
        });

    ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

    if (ui_canvas_status(c) < UiStatus_Pressed) {
      panel->panAny = false;
    }

    u32 streamCount = 0;
    for (u32 streamIdx = 0; streamIdx != dev_trace_max_streams; ++streamIdx) {
      streamCount += panel->streams[streamIdx].id >= 0;
    }
    const f32 height = ui_table_height(&table, streamCount);
    ui_scrollview_begin(c, &panel->scrollview, UiLayer_Normal, height);

    const UiId streamsBeginId = ui_canvas_id_peek(c);

    array_for_t(panel->streamSorting, u8, streamIdx) {
      DevTraceData* data = &panel->streams[*streamIdx];
      if (data->id < 0) {
        continue; // Unused stream slot.
      }
      ui_table_next_row(c, &table);
      ui_table_draw_row_bg(c, &table, ui_color(48, 48, 48, 192));

      const String streamName = mem_create(data->nameBuffer, data->nameLength);
      ui_label(c, streamName, .selectable = true);

      ui_table_next_column(c, &table);
      // NOTE: Counter the table padding so that events fill the whole cell horizontally.
      ui_layout_grow(
          c, UiAlign_MiddleCenter, ui_vector(g_tablePadding.x * 2, 0), UiBase_Absolute, Ui_X);
      trace_data_events_draw(c, panel, data, sinkStore);
    }

    ui_scrollview_end(c, &panel->scrollview);
    ui_layout_container_pop(c);
    ui_layout_container_pop(c);

    const UiId streamsLastId = ui_canvas_id_peek(c) - 1;
    panel->hoverAny = ui_canvas_group_status(c, streamsBeginId, streamsLastId) == UiStatus_Hovered;

    if (panel->hoverAny) {
      panel->scrollview.flags |= UiScrollviewFlags_BlockInput;
    } else {
      panel->scrollview.flags &= ~UiScrollviewFlags_BlockInput;
    }

  } else {
    panel->hoverAny = panel->panAny = false;
    ui_label(c, g_messageNoStoreSink, .align = UiAlign_MiddleCenter);
  }

  ui_panel_end(c, &panel->panel);
}

ecs_view_define(PanelQueryView) {
  ecs_access_write(DevTracePanelComp);
  ecs_access_read(DevPanelComp);
}

ecs_system_define(DevTracePanelQuerySys) {
  TraceSink* sinkStore = trace_sink_store_find(g_tracer);
  if (!sinkStore) {
    return;
  }

  EcsView* panelView = ecs_world_view_t(world, PanelQueryView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DevTracePanelComp* panel = ecs_view_write_t(itr, DevTracePanelComp);

    const bool pinned = ui_panel_pinned(&panel->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue; // No need to query data for hidden panels.
    }

    if (!panel->freeze || panel->refresh) {
      trace_data_clear(panel);
      panel->timeHead = time_steady_clock();

      trace_begin("sink_store_visit", TraceColor_Red);
      trace_sink_store_visit(sinkStore, trace_data_visitor, panel);
      trace_end();

      trace_stream_sort(panel);
      panel->refresh = false;
    }
  }
}

ecs_view_define(PanelDrawView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevTracePanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevTracePanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DevTracePanelDrawSys) {
  TraceSink* sinkStore = trace_sink_store_find(g_tracer);

  EcsView* panelView = ecs_world_view_t(world, PanelDrawView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId  entity = ecs_view_entity(itr);
    DevTracePanelComp* panel  = ecs_view_write_t(itr, DevTracePanelComp);
    UiCanvasComp*      canvas = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panel->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      panel->hoverAny = panel->panAny = false;
      panel->trigger.picking = panel->trigger.enabled = false;
      continue;
    }

    trace_panel_draw(canvas, panel, sinkStore);

    if (ui_panel_closed(&panel->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(dev_trace_module) {
  ecs_register_comp(DevTracePanelComp, .destructor = ecs_destruct_trace_panel);

  ecs_register_view(PanelQueryView);
  ecs_register_view(PanelDrawView);

  ecs_register_system(DevTracePanelQuerySys, ecs_view_id(PanelQueryView));
  ecs_order(DevTracePanelQuerySys, DevOrder_TraceQuery);

  ecs_register_system(DevTracePanelDrawSys, ecs_view_id(PanelDrawView));
}

EcsEntityId
dev_trace_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const u32 expectedEntryCount = dev_trace_default_depth * (g_jobsWorkerCount + 1) /* gpu */;
  const f32 panelHeight        = math_min(100 + 20.5f * expectedEntryCount, 675);

  const EcsEntityId  panelEntity = dev_panel_create(world, window, type);
  DevTracePanelComp* tracePanel  = ecs_world_add_t(
      world,
      panelEntity,
      DevTracePanelComp,
      .panel             = ui_panel(.size = ui_vector(800, panelHeight)),
      .scrollview        = ui_scrollview(),
      .eventDepth        = dev_trace_default_depth,
      .timeHead          = time_steady_clock(),
      .timeWindow        = time_milliseconds(100),
      .trigger.eventId   = sentinel_u8,
      .trigger.msgFilter = dynstring_create(g_allocHeap, 0),
      .trigger.threshold = time_milliseconds(20));

  tracePanel->streams = alloc_array_t(g_allocHeap, DevTraceData, dev_trace_max_streams);
  for (u32 streamIdx = 0; streamIdx != dev_trace_max_streams; ++streamIdx) {
    DevTraceData* streamData = &tracePanel->streams[streamIdx];
    streamData->id           = -1;
    streamData->events       = dynarray_create_t(g_allocHeap, TraceStoreEvent, 0);
  }

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&tracePanel->panel);
  }

  return panelEntity;
}
