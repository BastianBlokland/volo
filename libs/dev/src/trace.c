#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "core_format.h"
#include "core_math.h"
#include "dev_panel.h"
#include "dev_register.h"
#include "dev_trace.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "jobs.h"
#include "trace_dump.h"
#include "trace_sink_store.h"
#include "trace_tracer.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

// clang-format off

static const String g_tooltipFreeze       = string_static("Freeze the data set (halts data collection).");
static const String g_tooltipRefresh      = string_static("Refresh the data set.");
static const String g_tooltipTrigger      = string_static("Configure the trigger (auto freeze) settings.");
static const String g_tooltipTriggerPick  = string_static("Trigger on '{}' event.");
static const String g_tooltipTraceDump    = string_static("Dump performance trace data to disk (in the 'logs' directory).");
static const String g_messageNoStoreSink  = string_static("No store trace-sink found.\nNote: Check if the binary was compiled with the 'TRACE' option and not explicitly disabled.");

// clang-format on

#define debug_trace_max_name_length 15
#define debug_trace_max_threads 8
#define debug_trace_default_depth 4

typedef struct {
  ThreadId tid;
  u8       nameLength;
  u8       nameBuffer[debug_trace_max_name_length];
  DynArray events; // TraceStoreEvent[]
} DebugTraceData;

typedef struct {
  bool         enabled, picking;
  u8           eventId;
  DynString    msgFilter;
  TimeDuration threshold;
} DebugTraceTrigger;

ecs_comp_define(DebugTracePanelComp) {
  UiPanel           panel;
  UiScrollview      scrollview;
  bool              freeze, refresh;
  bool              hoverAny, panAny;
  u32               eventDepth;
  TimeSteady        timeHead;
  TimeDuration      timeWindow;
  DebugTraceTrigger trigger;
  DebugTraceData*   threads; // DebugTraceData[debug_trace_max_threads];
};

static void ecs_destruct_trace_panel(void* data) {
  DebugTracePanelComp* comp = data;
  dynstring_destroy(&comp->trigger.msgFilter);

  for (u32 threadIdx = 0; threadIdx != debug_trace_max_threads; ++threadIdx) {
    DebugTraceData* threadData = &comp->threads[threadIdx];
    dynarray_destroy(&threadData->events);
  }
  alloc_free_array_t(g_allocHeap, comp->threads, debug_trace_max_threads);
}

static void trace_trigger_set(DebugTraceTrigger* t, const u8 eventId) {
  t->eventId = eventId;
  t->enabled = true;
  t->picking = false;
}

static bool trace_trigger_match(const DebugTraceTrigger* t, const TraceStoreEvent* evt) {
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

static void trace_data_clear(DebugTracePanelComp* panel) {
  for (u32 threadIdx = 0; threadIdx != debug_trace_max_threads; ++threadIdx) {
    DebugTraceData* threadData = &panel->threads[threadIdx];
    threadData->tid            = 0;
    threadData->nameLength     = 0;
    dynarray_clear(&threadData->events);
  }
}

static void trace_data_focus(DebugTracePanelComp* panel, const TraceStoreEvent* evt) {
  panel->timeHead   = evt->timeStart + evt->timeDur;
  panel->timeWindow = math_clamp_i64(evt->timeDur, time_microsecond, time_milliseconds(500));
  panel->freeze     = true;
}

static void trace_data_visitor(
    const TraceSink*       sink,
    void*                  userCtx,
    const u32              bufferIdx,
    const ThreadId         threadId,
    const String           threadName,
    const TraceStoreEvent* evt) {
  (void)sink;
  DebugTracePanelComp* panel = userCtx;
  if (UNLIKELY(bufferIdx >= debug_trace_max_threads)) {
    diag_crash_msg("debug: Trace threads exceeds maximum");
  }
  DebugTraceData* threadData = &panel->threads[bufferIdx];
  if (!threadData->tid) {
    threadData->tid        = threadId;
    threadData->nameLength = (u8)math_min(threadName.size, debug_trace_max_name_length);
    mem_cpy(array_mem(threadData->nameBuffer), mem_slice(threadName, 0, threadData->nameLength));
  }
  *((TraceStoreEvent*)dynarray_push(&threadData->events, 1).ptr) = *evt;

  if (UNLIKELY(trace_trigger_match(&panel->trigger, evt))) {
    trace_data_focus(panel, evt);
  }
}

static UiColor trace_trigger_button_color(const DebugTraceTrigger* t) {
  UiColor color = ui_color(32, 32, 32, 192);
  if (t->picking) {
    color = ui_color(255, 16, 0, 192);
  } else if (t->enabled) {
    color = ui_color(16, 192, 0, 192);
  }
  return color;
}

static void trace_options_trigger_draw(
    UiCanvasComp* c, DebugTracePanelComp* panel, const TraceSink* sinkStore) {
  static const UiVector g_popupSize = {.x = 255.0f, .y = 130.0f};

  DebugTraceTrigger*      t           = &panel->trigger;
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
    ui_style_layer(c, UiLayer_Overlay);
    ui_canvas_min_interact_layer(c, UiLayer_Overlay);

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
trace_options_draw(UiCanvasComp* c, DebugTracePanelComp* panel, const TraceSink* sinkStore) {
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

static void trace_data_input_zoom(UiCanvasComp* c, DebugTracePanelComp* panel, const UiRect rect) {
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

static void trace_data_input_pan(UiCanvasComp* c, DebugTracePanelComp* panel, const UiRect rect) {
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
    UiCanvasComp*         c,
    DebugTracePanelComp*  panel,
    const DebugTraceData* data,
    const TraceSink*      sinkStore) {
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

    const String id  = trace_sink_store_id(sinkStore, evt->id);
    const String msg = mem_create(evt->msgData, evt->msgLength);
    if (barHovered && panel->freeze) {
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
trace_panel_draw(UiCanvasComp* c, DebugTracePanelComp* panel, const TraceSink* sinkStore) {
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
            {string_lit("Thread"), string_lit("Name of the thread.")},
            {string_lit("Events"), string_lit("Traced events on the thread.")},
        });

    ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

    if (ui_canvas_status(c) < UiStatus_Pressed) {
      panel->panAny = false;
    }

    const f32 height = ui_table_height(&table, g_jobsWorkerCount);
    ui_scrollview_begin(c, &panel->scrollview, UiLayer_Normal, height);

    const UiId threadsBeginId = ui_canvas_id_peek(c);

    for (u32 threadIdx = 0; threadIdx != debug_trace_max_threads; ++threadIdx) {
      DebugTraceData* data = &panel->threads[threadIdx];
      if (!data->tid) {
        continue; // Unused thread slot.
      }
      ui_table_next_row(c, &table);
      ui_table_draw_row_bg(c, &table, ui_color(48, 48, 48, 192));

      const String threadName = mem_create(data->nameBuffer, data->nameLength);
      ui_label(c, threadName, .selectable = true);

      ui_table_next_column(c, &table);
      // NOTE: Counter the table padding so that events fill the whole cell horizontally.
      ui_layout_grow(
          c, UiAlign_MiddleCenter, ui_vector(g_tablePadding.x * 2, 0), UiBase_Absolute, Ui_X);
      trace_data_events_draw(c, panel, data, sinkStore);
    }

    ui_scrollview_end(c, &panel->scrollview);
    ui_layout_container_pop(c);
    ui_layout_container_pop(c);

    const UiId threadsLastId = ui_canvas_id_peek(c) - 1;
    panel->hoverAny = ui_canvas_group_status(c, threadsBeginId, threadsLastId) == UiStatus_Hovered;

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
  ecs_access_write(DebugTracePanelComp);
  ecs_access_read(DevPanelComp);
}

ecs_system_define(DebugTracePanelQuerySys) {
  TraceSink* sinkStore = trace_sink_store_find(g_tracer);
  if (!sinkStore) {
    return;
  }

  EcsView* panelView = ecs_world_view_t(world, PanelQueryView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugTracePanelComp* panel = ecs_view_write_t(itr, DebugTracePanelComp);

    const bool pinned = ui_panel_pinned(&panel->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue; // No need to query data for hidden panels.
    }

    if (!panel->freeze || panel->refresh) {
      trace_data_clear(panel);
      panel->timeHead = time_steady_clock();
      trace_sink_store_visit(sinkStore, trace_data_visitor, panel);
      panel->refresh = false;
    }
  }
}

ecs_view_define(PanelDrawView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugTracePanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DebugTracePanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugTracePanelDrawSys) {
  TraceSink* sinkStore = trace_sink_store_find(g_tracer);

  EcsView* panelView = ecs_world_view_t(world, PanelDrawView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId    entity = ecs_view_entity(itr);
    DebugTracePanelComp* panel  = ecs_view_write_t(itr, DebugTracePanelComp);
    UiCanvasComp*        canvas = ecs_view_write_t(itr, UiCanvasComp);

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

ecs_module_init(debug_trace_module) {
  ecs_register_comp(DebugTracePanelComp, .destructor = ecs_destruct_trace_panel);

  ecs_register_view(PanelQueryView);
  ecs_register_view(PanelDrawView);

  ecs_register_system(DebugTracePanelQuerySys, ecs_view_id(PanelQueryView));
  ecs_order(DebugTracePanelQuerySys, DebugOrder_TraceQuery);

  ecs_register_system(DebugTracePanelDrawSys, ecs_view_id(PanelDrawView));
}

EcsEntityId
dev_trace_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const u32 panelHeight = math_min(100 + 20 * debug_trace_default_depth * g_jobsWorkerCount, 675);

  const EcsEntityId    panelEntity = dev_panel_create(world, window, type);
  DebugTracePanelComp* tracePanel  = ecs_world_add_t(
      world,
      panelEntity,
      DebugTracePanelComp,
      .panel             = ui_panel(.size = ui_vector(800, panelHeight)),
      .scrollview        = ui_scrollview(),
      .eventDepth        = debug_trace_default_depth,
      .timeHead          = time_steady_clock(),
      .timeWindow        = time_milliseconds(100),
      .trigger.eventId   = sentinel_u8,
      .trigger.msgFilter = dynstring_create(g_allocHeap, 0),
      .trigger.threshold = time_milliseconds(20));

  tracePanel->threads = alloc_array_t(g_allocHeap, DebugTraceData, debug_trace_max_threads);
  for (u32 threadIdx = 0; threadIdx != debug_trace_max_threads; ++threadIdx) {
    DebugTraceData* threadData = &tracePanel->threads[threadIdx];
    threadData->events         = dynarray_create_t(g_allocHeap, TraceStoreEvent, 0);
  }

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&tracePanel->panel);
  }

  return panelEntity;
}
