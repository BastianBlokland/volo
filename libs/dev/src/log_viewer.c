#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_sink.h"
#include "scene_time.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

#define log_tracker_mask (LogMask_Info | LogMask_Warn | LogMask_Error)
#define log_tracker_buffer_size (32 * usize_kibibyte)
#define log_tracker_max_age time_seconds(10)

typedef enum {
  DebugLogFlags_Combine = 1 << 0,
  DebugLogFlags_Default = DebugLogFlags_Combine,
} DebugLogFlags;

/**
 * Log entries are stored (sorted on timestamp) in a circular buffer. The range of active entries is
 * denoted by the 'entryHead' and 'entryTail' pointers, this range is contigious except for wrapping
 * at the end of the buffer. To detect when the range is wrapped we store a 'entryWrapPoint' pointer
 * which indicates that the next entry is at the beginning of the buffer.
 *
 * Entry memory layout:
 * base:              DebugLogEntry (24 bytes)
 * message-meta:      DebugLogStr   (1 byte)
 * message-data       byte[]        (n bytes)
 *
 * Followed n log parameters:
 * param-name-meta:   DebugLogStr   (1 byte)
 * param-name-data:   byte[]        (n bytes)
 * param-value-meta:  DebugLogStr   (1 byte)
 * param-value-data:  byte[]        (n bytes)
 */

typedef struct sDebugLogEntry DebugLogEntry;

struct sDebugLogEntry {
  TimeReal      timestamp;
  LogLevel      lvl : 8;
  DebugLogFlags flags : 8;
  u8            paramCount;
  u16           line;
  u16           fileNameLen;
  const u8*     fileNamePtr;
};

typedef struct {
  u8 length;
  u8 data[];
} DebugLogStr;

ASSERT(alignof(DebugLogStr) == 1, "Log strings should not require padding");

/**
 * Sink that will receive log messages.
 * NOTE: Needs a stable pointer as it will be registered to the logger.
 * NOTE: Sink needs to stay alive as long as the logger still exists or the tracker still exists,
 * to achieve this it has a basic ref-counter.
 */
typedef struct {
  LogSink        api;
  i32            refCounter;
  ThreadSpinLock bufferLock;
  u8*            buffer;    // Circular buffer of (dynamically sized) entries.
  u8*            bufferPos; // Current write position.
  DebugLogEntry *entryHead, *entryTail, *entryWrapPoint;
} DebugLogSink;

static DebugLogStr* debug_log_str_next(DebugLogStr* str) {
  return bits_ptr_offset(str, sizeof(DebugLogStr) + str->length);
}

static String debug_log_text(const DebugLogStr* str) { return mem_create(str->data, str->length); }

static bool debug_log_str_eq(DebugLogStr* a, DebugLogStr* b) {
  return a->length == b->length && mem_eq(debug_log_text(a), debug_log_text(b));
}

static DebugLogStr* debug_log_msg(const DebugLogEntry* entry) {
  return bits_ptr_offset(entry, sizeof(DebugLogEntry));
}

static bool debug_log_is_dup(const DebugLogEntry* a, const DebugLogEntry* b) {
  return debug_log_str_eq(debug_log_msg(a), debug_log_msg(b));
}

static DebugLogEntry* debug_log_next(DebugLogSink* s, DebugLogEntry* entry) {
  if (entry == s->entryTail) {
    return null;
  }
  if (entry == s->entryWrapPoint) {
    return (DebugLogEntry*)s->buffer; // Next entry is the beginning of the buffer.
  }
  DebugLogStr* strItr = debug_log_msg(entry);
  for (u32 i = 0; i != entry->paramCount; ++i) {
    strItr = debug_log_str_next(strItr); // Param name.
    strItr = debug_log_str_next(strItr); // Param value.
  }
  strItr = debug_log_str_next(strItr); // Skip over the last string.
  return bits_align_ptr(strItr, alignof(DebugLogEntry));
}

static Mem debug_log_buffer_remaining(DebugLogSink* s) {
  // Check if the bufferPos is before or after the range of active entries in the buffer.
  if (!s->entryHead || s->bufferPos > (const u8*)s->entryHead) {
    return mem_from_to(s->bufferPos, s->buffer + log_tracker_buffer_size);
  }
  return mem_from_to(s->bufferPos, s->entryHead);
}

static void debug_log_write_text(DynString* out, const String text) {
  DebugLogStr* ptr = dynstring_push(out, sizeof(DebugLogStr)).ptr;
  diag_assert(bits_aligned_ptr(ptr, alignof(DebugLogStr)));

  ptr->length = (u8)math_min(u8_max, text.size);
  dynstring_append(out, string_slice(text, 0, ptr->length));
}

static void debug_log_write_arg(DynString* out, const FormatArg* arg) {
  DebugLogStr* ptr = dynstring_push(out, sizeof(DebugLogStr)).ptr;
  diag_assert(bits_aligned_ptr(ptr, alignof(DebugLogStr)));

  const usize sizeStart = out->size;
  format_write_arg(out, arg);
  ptr->length = (u8)math_min(u8_max, out->size - sizeStart);
  out->size   = sizeStart + ptr->length; // Erase the part that did not fit.
}

static void debug_log_write_entry(
    DynString*      out,
    const LogLevel  lvl,
    const SourceLoc srcLoc,
    const TimeReal  timestamp,
    const String    msg,
    const LogParam* params) {
  DebugLogEntry* ptr = dynstring_push(out, sizeof(DebugLogEntry)).ptr;
  diag_assert(bits_aligned_ptr(ptr, alignof(DebugLogEntry)));

  *ptr = (DebugLogEntry){
      .timestamp   = timestamp,
      .lvl         = lvl,
      .flags       = DebugLogFlags_Default,
      .line        = (u16)math_min(srcLoc.line, u16_max),
      .fileNameLen = (u16)math_min(srcLoc.file.size, u16_max),
      .fileNamePtr = srcLoc.file.ptr,
  };

  debug_log_write_text(out, msg);

  for (const LogParam* itr = params; itr->arg.type; ++itr) {
    debug_log_write_text(out, itr->name);
    debug_log_write_arg(out, &itr->arg);
    ++ptr->paramCount;
  }
}

static void debug_log_prune_older(DebugLogSink* s, const TimeReal timestamp) {
  thread_spinlock_lock(&s->bufferLock);
  if (s->entryHead) {
    for (; s->entryHead->timestamp < timestamp;) {
      if (s->entryHead == s->entryTail) {
        s->entryHead = s->entryTail = s->entryWrapPoint = null; // Whole buffer became empty.
        break;
      }
      DebugLogEntry* next = debug_log_next(s, s->entryHead);
      if (s->entryHead == s->entryWrapPoint) {
        s->entryWrapPoint = null; // Entry head has wrapped around.
      }
      s->entryHead = next;
    }
  }
  thread_spinlock_unlock(&s->bufferLock);
}

static void debug_log_sink_write(
    LogSink*        sink,
    const LogLevel  lvl,
    const SourceLoc srcLoc,
    const TimeReal  timestamp,
    const String    msg,
    const LogParam* params) {
  DebugLogSink* s = (DebugLogSink*)sink;
  if ((log_tracker_mask & (1 << lvl)) == 0) {
    return;
  }
  const Mem scratchMem = alloc_alloc(g_allocScratch, 4 * usize_kibibyte, 1);
  DynString scratchStr = dynstring_create_over(scratchMem);
  debug_log_write_entry(&scratchStr, lvl, srcLoc, timestamp, msg, params);

  thread_spinlock_lock(&s->bufferLock);
  {
    if (!s->entryHead) {
      s->bufferPos = s->buffer; // Buffer is empty; start at the beginning.
    } else {
      // Start from the beginning of the last entry (with padding to satisfy alignment).
      s->bufferPos = bits_align_ptr(s->bufferPos, alignof(DebugLogEntry));
    }

  Write:;
    const Mem buffer = debug_log_buffer_remaining(s);
    if (LIKELY(buffer.size >= scratchStr.size)) {
      mem_cpy(buffer, dynstring_view(&scratchStr));
      s->bufferPos += scratchStr.size;

      DebugLogEntry* entry = (DebugLogEntry*)buffer.ptr;
      if (!s->entryHead) {
        s->entryHead = entry;
      }
      s->entryTail = entry;

      thread_atomic_fence_release(); // Synchronize with read-only observers.
    } else if (s->entryHead && buffer.ptr > (void*)s->entryHead) {
      s->bufferPos      = s->buffer;    // Wrap around to the beginning.
      s->entryWrapPoint = s->entryTail; // Mark the last entry before the wrap.
      goto Write;                       // Retry the write.
    }
    // NOTE: Message gets dropped if there is not enough space in the buffer.
  }
  thread_spinlock_unlock(&s->bufferLock);
}

static void debug_log_sink_destroy(LogSink* sink) {
  DebugLogSink* debugSink = (DebugLogSink*)sink;
  if (thread_atomic_sub_i32(&debugSink->refCounter, 1) == 1) {
    alloc_free(g_allocHeap, mem_create(debugSink->buffer, log_tracker_buffer_size));
    alloc_free_t(g_allocHeap, debugSink);
  }
}

DebugLogSink* debug_log_sink_create(void) {
  DebugLogSink* sink = alloc_alloc_t(g_allocHeap, DebugLogSink);

  *sink = (DebugLogSink){
      .api    = {.write = debug_log_sink_write, .destroy = debug_log_sink_destroy},
      .buffer = alloc_alloc(g_allocHeap, log_tracker_buffer_size, alignof(DebugLogEntry)).ptr,
  };

  return sink;
}

ecs_comp_define(DebugLogTrackerComp) {
  bool          freeze;
  TimeDuration  freezeTime;
  DebugLogSink* sink;
};

ecs_comp_define(DebugLogViewerComp) {
  LogMask        mask;
  TimeZone       timezone;
  DebugLogEntry* inspectEntry;
  UiPanel        inspectPanel;
  UiScrollview   inspectScrollView;
};

static void ecs_destruct_log_tracker(void* data) {
  DebugLogTrackerComp* comp = data;
  debug_log_sink_destroy((LogSink*)comp->sink);
}

ecs_view_define(LogGlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_write(DebugLogTrackerComp);
}

ecs_view_define(LogViewerView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugLogViewerComp's are exclusively managed here.

  ecs_access_write(DebugLogViewerComp);
  ecs_access_write(UiCanvasComp);
}

static DebugLogTrackerComp*
debug_log_tracker_create(EcsWorld* world, const EcsEntityId entity, Logger* logger) {
  DebugLogSink* sink = debug_log_sink_create();
  thread_atomic_store_i32(&sink->refCounter, 2); // Referenced by the logger and the viewer.
  log_add_sink(logger, (LogSink*)sink);
  return ecs_world_add_t(world, entity, DebugLogTrackerComp, .sink = sink);
}

static String debug_log_inspect_title(const LogLevel lvl) {
  switch (lvl) {
  case LogLevel_Debug:
    return fmt_write_scratch("{} Debug", fmt_ui_shape(Article));
  case LogLevel_Info:
    return fmt_write_scratch("{} Info", fmt_ui_shape(Article));
  case LogLevel_Warn:
    return fmt_write_scratch("{} Warning", fmt_ui_shape(Error));
  case LogLevel_Error:
    return fmt_write_scratch("{} Error", fmt_ui_shape(Error));
  case LogLevel_Count:
    break;
  }
  diag_crash();
}

static UiColor debug_log_inspect_title_color(const LogLevel lvl) {
  switch (lvl) {
  case LogLevel_Debug:
    return ui_color(0, 0, 100, 192);
  case LogLevel_Info:
    return ui_color(0, 100, 0, 192);
  case LogLevel_Warn:
    return ui_color(128, 128, 0, 192);
  case LogLevel_Error:
    return ui_color(100, 0, 0, 192);
  case LogLevel_Count:
    break;
  }
  diag_crash();
}

static void debug_log_inspect_param(UiCanvasComp* c, UiTable* t, const String k, const String v) {
  ui_table_next_row(c, t);
  ui_table_draw_row_bg(c, t, ui_color(48, 48, 48, 192));
  ui_label(c, k);
  ui_table_next_column(c, t);
  ui_label(c, v, .selectable = true);
}

static f32 debug_log_inspect_desired_height(const DebugLogEntry* entry) {
  UiTable table = ui_table(); // Dummy table.
  return ui_table_height(&table, 3 /* builtin params */ + entry->paramCount);
}

static void debug_log_inspect_open(DebugLogViewerComp* viewer, DebugLogEntry* entry) {
  const f32 heightDesired = debug_log_inspect_desired_height(entry);
  const f32 heightMax     = 600.0f;
  const f32 height        = math_min(heightDesired, heightMax);

  viewer->inspectEntry      = entry;
  viewer->inspectPanel      = ui_panel(.size = ui_vector(700, height));
  viewer->inspectScrollView = ui_scrollview();
}

static void debug_log_inspect_close(DebugLogViewerComp* viewer) { viewer->inspectEntry = null; }

static void debug_log_inspect_draw(UiCanvasComp* c, DebugLogViewerComp* viewer) {
  const DebugLogEntry* entry = viewer->inspectEntry;

  ui_panel_begin(
      c,
      &viewer->inspectPanel,
      .title       = debug_log_inspect_title(entry->lvl),
      .topBarColor = debug_log_inspect_title_color(entry->lvl),
      .pinnable    = false);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const f32 totalHeight = ui_table_height(&table, 3 /* builtin params */ + entry->paramCount);
  ui_scrollview_begin(c, &viewer->inspectScrollView, UiLayer_Normal, totalHeight);

  DebugLogStr* strItr = debug_log_msg(entry);
  debug_log_inspect_param(c, &table, string_lit("message"), debug_log_text(strItr));

  for (u32 paramIdx = 0; paramIdx != entry->paramCount; ++paramIdx) {
    DebugLogStr* paramName = debug_log_str_next(strItr);
    DebugLogStr* paramVal  = debug_log_str_next(paramName);
    debug_log_inspect_param(c, &table, debug_log_text(paramName), debug_log_text(paramVal));
    strItr = paramVal;
  }

  const FormatTimeTerms timeTerms = FormatTimeTerms_Time | FormatTimeTerms_Milliseconds;
  const String          timeVal   = fmt_write_scratch(
      "{}", fmt_time(entry->timestamp, .terms = timeTerms, .timezone = viewer->timezone));
  debug_log_inspect_param(c, &table, string_lit("time"), timeVal);

  const String fileName  = mem_create(entry->fileNamePtr, entry->fileNameLen);
  const String sourceVal = fmt_write_scratch("{}:{}", fmt_path(fileName), fmt_int(entry->line));
  debug_log_inspect_param(c, &table, string_lit("source"), sourceVal);

  ui_scrollview_end(c, &viewer->inspectScrollView);
  ui_panel_end(c, &viewer->inspectPanel);

  if (ui_panel_closed(&viewer->inspectPanel)) {
    debug_log_inspect_close(viewer); // Close when requested.
  }
  if (ui_canvas_input_any(c) && ui_canvas_status(c) == UiStatus_Idle) {
    debug_log_inspect_close(viewer); // Close when clicking outside the panel.
  }
}

static void debug_log_notif_tooltip(
    UiCanvasComp* c, const UiId id, const DebugLogViewerComp* viewer, const DebugLogEntry* entry) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, 4 * usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  DebugLogStr* strItr = debug_log_msg(entry);
  fmt_write(&buffer, "\a.bmessage\ar: {}\n", fmt_text(debug_log_text(strItr)));

  for (u32 paramIdx = 0; paramIdx != entry->paramCount; ++paramIdx) {
    DebugLogStr* paramName = debug_log_str_next(strItr);
    DebugLogStr* paramVal  = debug_log_str_next(paramName);
    fmt_write(
        &buffer,
        "\a.b{}\ar: {}\n",
        fmt_text(debug_log_text(paramName)),
        fmt_text(debug_log_text(paramVal)));
    strItr = paramVal;
  }

  const FormatTimeTerms timeTerms = FormatTimeTerms_Time | FormatTimeTerms_Milliseconds;
  fmt_write(
      &buffer,
      "\a.btime\ar: {}\n",
      fmt_time(entry->timestamp, .terms = timeTerms, .timezone = viewer->timezone));

  const String fileName = mem_create(entry->fileNamePtr, entry->fileNameLen);
  fmt_write(&buffer, "\a.bsource\ar: {}:{}\n", fmt_path(fileName), fmt_int(entry->line));

  ui_tooltip(c, id, dynstring_view(&buffer), .maxSize = ui_vector(750, 750));
}

static UiColor debug_log_notif_bg_color(const LogLevel lvl) {
  switch (lvl) {
  case LogLevel_Debug:
    return ui_color(0, 0, 48, 230);
  case LogLevel_Info:
    return ui_color(0, 48, 0, 230);
  case LogLevel_Warn:
    return ui_color(96, 96, 0, 230);
  case LogLevel_Error:
    return ui_color(48, 0, 0, 230);
  case LogLevel_Count:
    break;
  }
  diag_crash();
}

static void debug_log_notif_draw_entry(
    UiCanvasComp* c, DebugLogViewerComp* viewer, DebugLogEntry* entry, const u32 repeat) {
  DebugLogStr* msg = debug_log_msg(entry);

  ui_style_push(c);
  ui_style_color(c, debug_log_notif_bg_color(entry->lvl));
  const UiId bgId = ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_Interactable);
  ui_style_pop(c);

  ui_layout_push(c);
  ui_layout_grow(c, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);

  String text;
  if (repeat) {
    text = fmt_write_scratch("x{} {}", fmt_int(repeat + 1), fmt_text(debug_log_text(msg)));
  } else {
    text = debug_log_text(msg);
  }
  ui_canvas_draw_text(c, text, 15, UiAlign_MiddleLeft, UiFlags_None);
  ui_layout_pop(c);

  const UiStatus status = ui_canvas_elem_status(c, bgId);
  if (status == UiStatus_Activated) {
    if (repeat) {
      entry->flags &= ~DebugLogFlags_Combine;
    } else {
      debug_log_inspect_open(viewer, entry);
    }
    ui_canvas_sound(c, UiSoundType_Click);
  }
  if (status >= UiStatus_Hovered) {
    debug_log_notif_tooltip(c, bgId, viewer, entry);
  } else {
    ui_canvas_id_skip(c, 2); // NOTE: Tooltips consume two ids.
  }
}

static void debug_log_notif_draw(
    UiCanvasComp* c, const DebugLogTrackerComp* tracker, DebugLogViewerComp* viewer) {
  ui_layout_move_to(c, UiBase_Container, UiAlign_TopRight, Ui_XY);
  ui_layout_resize(c, UiAlign_TopRight, ui_vector(400, 0), UiBase_Absolute, Ui_X);
  ui_layout_resize(c, UiAlign_TopLeft, ui_vector(0, 20), UiBase_Absolute, Ui_Y);

  ui_style_push(c);
  ui_style_outline(c, 0);

  /**
   * Because 'debug_log_sink_write' only adds new entries (but never removes) and this is never
   * called in parallel with 'debug_log_prune_older' we can avoid taking the spinlock and instead
   * iterate up to the last fully written one.
   */
  thread_atomic_fence_acquire();
  DebugLogEntry* first = tracker->sink->entryHead;
  DebugLogEntry* last  = tracker->sink->entryTail;
  if (!first) {
    goto End; // Buffer is emtpy.
  }
  for (DebugLogEntry* itr = first;; itr = debug_log_next(tracker->sink, itr)) {
    if (viewer->mask & (1 << itr->lvl)) {
      DebugLogEntry* entry  = itr;
      u32            repeat = 0;
      if (entry->flags & DebugLogFlags_Combine) {
        for (;;) {
          if (itr == last) {
            break;
          }
          DebugLogEntry* next = debug_log_next(tracker->sink, itr);
          if (debug_log_is_dup(entry, next)) {
            itr = next;
            ++repeat;
          } else {
            break;
          }
        }
      }
      debug_log_notif_draw_entry(c, viewer, entry, repeat);
      ui_layout_next(c, Ui_Down, 0);
    }
    if (itr == last) {
      break; // Reached the last written one when we synchronized with debug_log_sink_write.
    }
  }
End:
  ui_style_pop(c);
}

ecs_system_define(DebugLogUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, LogGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  DebugLogTrackerComp* tracker = ecs_view_write_t(globalItr, DebugLogTrackerComp);
  const SceneTimeComp* time    = ecs_view_read_t(globalItr, SceneTimeComp);
  if (tracker->freeze) {
    tracker->freezeTime += time->realDelta;
  }
  const TimeReal now          = time_real_clock();
  const TimeReal oldestToKeep = time_real_offset(now, -(tracker->freezeTime + log_tracker_max_age));
  debug_log_prune_older(tracker->sink, oldestToKeep);

  tracker->freeze   = false;
  EcsView* drawView = ecs_world_view_t(world, LogViewerView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    DebugLogViewerComp* viewer = ecs_view_write_t(itr, DebugLogViewerComp);
    UiCanvasComp*       canvas = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    ui_canvas_to_front(canvas); // Always draw logs on-top.

    // Draw notifications (new log entries).
    const UiId notifIdFirst = ui_canvas_id_peek(canvas);
    debug_log_notif_draw(canvas, tracker, viewer);
    const UiId notifIdLast = ui_canvas_id_peek(canvas) - 1;
    if (ui_canvas_group_status(canvas, notifIdFirst, notifIdLast) >= UiStatus_Hovered) {
      tracker->freeze = true; // Don't remove entries while hovering any of the notifications.
    }

    // Draw inspector.
    if (viewer->inspectEntry) {
      tracker->freeze = true; // Don't remove entries while inspecting.
      debug_log_inspect_draw(canvas, viewer);
    }
  }
}

ecs_module_init(debug_log_viewer_module) {
  ecs_register_comp(DebugLogTrackerComp, .destructor = ecs_destruct_log_tracker);
  ecs_register_comp(DebugLogViewerComp);

  ecs_register_view(LogGlobalView);
  ecs_register_view(LogViewerView);

  ecs_register_system(DebugLogUpdateSys, ecs_view_id(LogGlobalView), ecs_view_id(LogViewerView));
}

void debug_log_tracker_init(EcsWorld* world, Logger* logger) {
  debug_log_tracker_create(world, ecs_world_global(world), logger);
}

EcsEntityId debug_log_viewer_create(EcsWorld* world, const EcsEntityId window, const LogMask mask) {
  const EcsEntityId viewerEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, viewerEntity, DebugLogViewerComp, .mask = mask, .timezone = time_zone_current());
  return viewerEntity;
}

void debug_log_viewer_set_mask(DebugLogViewerComp* viewer, const LogMask mask) {
  viewer->mask = mask;
}
