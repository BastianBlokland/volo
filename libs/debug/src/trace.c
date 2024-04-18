#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_math.h"
#include "debug_register.h"
#include "debug_trace.h"
#include "ecs_world.h"
#include "trace.h"
#include "ui.h"

// clang-format off

static const String g_tooltipFreeze       = string_static("Freeze the data set (halts data collection).");
static const String g_tooltipTraceDump    = string_static("Dump performance trace data to disk (in the 'logs' directory).");
static const String g_messageNoStoreSink  = string_static("No store trace-sink found.\nNote: Was the binary compiled with the 'TRACE' option?");

// clang-format on

#define debug_trace_max_name_length 15
#define debug_trace_max_threads 8

typedef struct {
  ThreadId tid;
  u8       nameLength;
  u8       nameBuffer[debug_trace_max_name_length];
  DynArray events; // TraceStoreEvent[]
} DebugTraceData;

ecs_comp_define(DebugTracePanelComp) {
  UiPanel        panel;
  bool           freeze;
  TimeSteady     timeHead;
  TimeDuration   timeWindow;
  DebugTraceData threads[debug_trace_max_threads];
};

static void ecs_destruct_trace_panel(void* data) {
  DebugTracePanelComp* comp = data;
  array_for_t(comp->threads, DebugTraceData, thread) { dynarray_destroy(&thread->events); }
}

static UiColor trace_event_color(const TraceColor col) {
  switch (col) {
  case TraceColor_Default:
  case TraceColor_White:
    return ui_color(255, 255, 255, 178);
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
  array_for_t(panel->threads, DebugTraceData, thread) {
    thread->tid        = 0;
    thread->nameLength = 0;
    dynarray_clear(&thread->events);
  }
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
  *dynarray_push_t(&threadData->events, TraceStoreEvent) = *evt;
}

static void
trace_options_draw(UiCanvasComp* c, DebugTracePanelComp* panel, const TraceSink* sinkStore) {
  ui_layout_push(c);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Fixed, 175);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  ui_table_next_row(c, &table);

  if (ui_button(c, .label = string_lit("Dump (eventtrace)"), .tooltip = g_tooltipTraceDump)) {
    trace_dump_eventtrace_to_path_default(sinkStore);
  }

  ui_table_next_column(c, &table);
  const String timeLabel = fmt_write_scratch(
      "Window: {}", fmt_duration(panel->timeWindow, .minDecDigits = 1, .maxDecDigits = 1));
  ui_label(c, timeLabel);

  ui_table_next_column(c, &table);
  ui_label(c, string_lit("Freeze:"));
  ui_table_next_column(c, &table);
  ui_toggle(c, &panel->freeze, .tooltip = g_tooltipFreeze);

  ui_layout_pop(c);
}

static void trace_data_events_draw(
    UiCanvasComp*         c,
    DebugTracePanelComp*  panel,
    const DebugTraceData* data,
    const TraceSink*      sinkStore) {
  (void)sinkStore;

  ui_layout_push(c);
  ui_layout_container_push(c, UiClip_Rect);
  ui_style_push(c);
  ui_style_outline(c, 1);

  ui_canvas_id_block_next(c); // Start events on their own id-block.
  ui_canvas_draw_glyph(c, UiShape_Empty, 0, UiFlags_Interactable); // Invisible elem as scroll tgt.

  // Scroll and pan input.
  const UiStatus blockStatus = ui_canvas_group_block_status(c);
  if (blockStatus == UiStatus_Hovered) {
    const f32 scrollSpeed = 2.5f;
    panel->timeWindow -= (TimeDuration)time_milliseconds(ui_canvas_input_scroll(c).y * scrollSpeed);
    panel->timeWindow = math_clamp_i64(panel->timeWindow, time_millisecond, time_milliseconds(250));
  }
  if (panel->freeze) {
    if (blockStatus >= UiStatus_Hovered) {
      ui_canvas_interact_type(c, UiInteractType_Move);
    }
    if (blockStatus >= UiStatus_Pressed) {
      const f32 panSpeed = 1.0f;
      panel->timeHead -= (TimeDuration)time_milliseconds(ui_canvas_input_delta(c).x * panSpeed);
    }
  }

  // NOTE: Timestamps are in nanoseconds.
  const f64 timeLeft  = (f64)(panel->timeHead - panel->timeWindow);
  const f64 timeRight = (f64)panel->timeHead;

  dynarray_for_t(&data->events, TraceStoreEvent, evt) {
    const f64 fracLeft  = math_unlerp(timeLeft, timeRight, (f64)evt->timeStart);
    const f64 fracRight = math_unlerp(timeLeft, timeRight, (f64)(evt->timeStart + evt->timeDur));

    if (fracRight <= 0.0 || fracLeft >= 1.0) {
      continue; // Event outside of the visible region.
    }

    const UiVector size = {.width = (f32)(fracRight - fracLeft), .height = 0.2f};
    const UiVector pos  = {.x = (f32)fracLeft, .y = 1.0f - size.height * (evt->stackDepth + 1)};

    ui_layout_set(c, ui_rect(pos, size), UiBase_Container);

    ui_style_color(c, trace_event_color(evt->color));
    ui_canvas_draw_glyph(c, UiShape_Square, 5, UiFlags_None);
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
    ui_layout_container_push(c, UiClip_None);

    static const UiVector g_tablePadding = {10, 5};
    UiTable               table          = ui_table(.spacing = g_tablePadding, .rowHeight = 100);
    ui_table_add_column(&table, UiTableColumn_Fixed, 125);
    ui_table_add_column(&table, UiTableColumn_Flexible, 0);

    ui_table_draw_header(
        c,
        &table,
        (const UiTableColumnName[]){
            {string_lit("Thread"), string_lit("Name of the thread.")},
            {string_lit("Events"), string_lit("Traced events on the thread.")},
        });

    ui_layout_container_push(c, UiClip_None);

    array_for_t(panel->threads, DebugTraceData, data) {
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
    ui_canvas_id_block_next(c); // End on an consistent id.

    ui_layout_container_pop(c);
    ui_layout_container_pop(c);
  } else {
    ui_label(c, g_messageNoStoreSink, .align = UiAlign_MiddleCenter);
  }

  ui_panel_end(c, &panel->panel);
}

ecs_view_define(PanelQueryView) {
  ecs_access_write(DebugTracePanelComp);
  ecs_access_read(DebugPanelComp);
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
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
      continue;
    }

    if (!panel->freeze) {
      trace_data_clear(panel);
      panel->timeHead = time_steady_clock();
      trace_sink_store_visit(sinkStore, trace_data_visitor, panel);
    }
  }
}

ecs_view_define(PanelDrawView) {
  ecs_access_read(DebugPanelComp);
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
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
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
debug_trace_panel_open(EcsWorld* world, const EcsEntityId window, const DebugPanelType type) {
  const EcsEntityId    panelEntity = debug_panel_create(world, window, type);
  DebugTracePanelComp* tracePanel  = ecs_world_add_t(
      world,
      panelEntity,
      DebugTracePanelComp,
      .panel      = ui_panel(.size = ui_vector(800, 500)),
      .timeHead   = time_steady_clock(),
      .timeWindow = time_milliseconds(100));

  array_for_t(tracePanel->threads, DebugTraceData, thread) {
    thread->events = dynarray_create_t(g_alloc_heap, TraceStoreEvent, 0);
  }

  if (type == DebugPanelType_Detached) {
    ui_panel_maximize(&tracePanel->panel);
  }

  return panelEntity;
}
