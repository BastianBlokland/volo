#include "core_alloc.h"
#include "core_array.h"
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
  DevLogFlags_Combine = 1 << 0,
  DevLogFlags_Default = DevLogFlags_Combine,
} DevLogFlags;

/**
 * Log entries are stored (sorted on timestamp) in a circular buffer. The range of active entries is
 * denoted by the 'entryHead' and 'entryTail' pointers, this range is contigious except for wrapping
 * at the end of the buffer. To detect when the range is wrapped we store a 'entryWrapPoint' pointer
 * which indicates that the next entry is at the beginning of the buffer.
 *
 * Entry memory layout:
 * base:              DevLogEntry (24 bytes)
 * message-meta:      DevLogStr   (1 byte)
 * message-data       byte[]      (n bytes)
 *
 * Followed n log parameters:
 * param-name-meta:   DevLogStr   (1 byte)
 * param-name-data:   byte[]      (n bytes)
 * param-value-meta:  DevLogStr   (1 byte)
 * param-value-data:  byte[]      (n bytes)
 */

typedef struct sDevLogEntry DevLogEntry;

struct sDevLogEntry {
  TimeReal    timestamp;
  LogLevel    lvl : 8;
  DevLogFlags flags : 8;
  u8          paramCount;
  u16         line;
  u16         fileNameLen;
  const u8*   fileNamePtr;
};

typedef struct {
  u8 length;
  u8 data[];
} DevLogStr;

ASSERT(alignof(DevLogStr) == 1, "Log strings should not require padding");

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
  DevLogEntry *  entryHead, *entryTail, *entryWrapPoint;
} DevLogSink;

static DevLogStr* dev_log_str_next(DevLogStr* str) {
  return bits_ptr_offset(str, sizeof(DevLogStr) + str->length);
}

static String dev_log_text(const DevLogStr* str) { return mem_create(str->data, str->length); }

static bool dev_log_str_eq(DevLogStr* a, DevLogStr* b) {
  return a->length == b->length && mem_eq(dev_log_text(a), dev_log_text(b));
}

static DevLogStr* dev_log_msg(const DevLogEntry* entry) {
  return bits_ptr_offset(entry, sizeof(DevLogEntry));
}

static bool dev_log_is_dup(const DevLogEntry* a, const DevLogEntry* b) {
  return dev_log_str_eq(dev_log_msg(a), dev_log_msg(b));
}

static DevLogEntry* dev_log_next(DevLogSink* s, DevLogEntry* entry) {
  if (entry == s->entryTail) {
    return null;
  }
  if (entry == s->entryWrapPoint) {
    return (DevLogEntry*)s->buffer; // Next entry is the beginning of the buffer.
  }
  DevLogStr* strItr = dev_log_msg(entry);
  for (u32 i = 0; i != entry->paramCount; ++i) {
    strItr = dev_log_str_next(strItr); // Param name.
    strItr = dev_log_str_next(strItr); // Param value.
  }
  strItr = dev_log_str_next(strItr); // Skip over the last string.
  return bits_align_ptr(strItr, alignof(DevLogEntry));
}

static Mem dev_log_buffer_remaining(DevLogSink* s) {
  // Check if the bufferPos is before or after the range of active entries in the buffer.
  if (!s->entryHead || s->bufferPos > (const u8*)s->entryHead) {
    return mem_from_to(s->bufferPos, s->buffer + log_tracker_buffer_size);
  }
  return mem_from_to(s->bufferPos, s->entryHead);
}

static void dev_log_write_text(DynString* out, const String text) {
  DevLogStr* ptr = dynstring_push(out, sizeof(DevLogStr)).ptr;
  diag_assert(bits_aligned_ptr(ptr, alignof(DevLogStr)));

  ptr->length = (u8)math_min(u8_max, text.size);
  dynstring_append(out, string_slice(text, 0, ptr->length));
}

static void dev_log_write_arg(DynString* out, const FormatArg* arg) {
  DevLogStr* ptr = dynstring_push(out, sizeof(DevLogStr)).ptr;
  diag_assert(bits_aligned_ptr(ptr, alignof(DevLogStr)));

  const usize sizeStart = out->size;
  format_write_arg(out, arg);
  ptr->length = (u8)math_min(u8_max, out->size - sizeStart);
  out->size   = sizeStart + ptr->length; // Erase the part that did not fit.
}

static void dev_log_write_entry(
    DynString*      out,
    const LogLevel  lvl,
    const SourceLoc srcLoc,
    const TimeReal  timestamp,
    const String    msg,
    const LogParam* params) {
  DevLogEntry* ptr = dynstring_push(out, sizeof(DevLogEntry)).ptr;
  diag_assert(bits_aligned_ptr(ptr, alignof(DevLogEntry)));

  *ptr = (DevLogEntry){
      .timestamp   = timestamp,
      .lvl         = lvl,
      .flags       = DevLogFlags_Default,
      .line        = (u16)math_min(srcLoc.line, u16_max),
      .fileNameLen = (u16)math_min(srcLoc.file.size, u16_max),
      .fileNamePtr = srcLoc.file.ptr,
  };

  dev_log_write_text(out, msg);

  for (const LogParam* itr = params; itr->arg.type; ++itr) {
    dev_log_write_text(out, itr->name);
    dev_log_write_arg(out, &itr->arg);
    ++ptr->paramCount;
  }
}

static void dev_log_prune_older(DevLogSink* s, const TimeReal timestamp) {
  thread_spinlock_lock(&s->bufferLock);
  if (s->entryHead) {
    for (; s->entryHead->timestamp < timestamp;) {
      if (s->entryHead == s->entryTail) {
        s->entryHead = s->entryTail = s->entryWrapPoint = null; // Whole buffer became empty.
        break;
      }
      DevLogEntry* next = dev_log_next(s, s->entryHead);
      if (s->entryHead == s->entryWrapPoint) {
        s->entryWrapPoint = null; // Entry head has wrapped around.
      }
      s->entryHead = next;
    }
  }
  thread_spinlock_unlock(&s->bufferLock);
}

static void dev_log_sink_write(
    LogSink*        sink,
    const LogLevel  lvl,
    const SourceLoc srcLoc,
    const TimeReal  timestamp,
    const String    msg,
    const LogParam* params) {
  DevLogSink* s = (DevLogSink*)sink;
  if ((log_tracker_mask & (1 << lvl)) == 0) {
    return;
  }
  const Mem scratchMem = alloc_alloc(g_allocScratch, 4 * usize_kibibyte, 1);
  DynString scratchStr = dynstring_create_over(scratchMem);
  dev_log_write_entry(&scratchStr, lvl, srcLoc, timestamp, msg, params);

  thread_spinlock_lock(&s->bufferLock);
  {
    if (!s->entryHead) {
      s->bufferPos = s->buffer; // Buffer is empty; start at the beginning.
    } else {
      // Start from the beginning of the last entry (with padding to satisfy alignment).
      s->bufferPos = bits_align_ptr(s->bufferPos, alignof(DevLogEntry));
    }

  Write:;
    const Mem buffer = dev_log_buffer_remaining(s);
    if (LIKELY(buffer.size >= scratchStr.size)) {
      mem_cpy(buffer, dynstring_view(&scratchStr));
      s->bufferPos += scratchStr.size;

      DevLogEntry* entry = (DevLogEntry*)buffer.ptr;
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

static void dev_log_sink_destroy(LogSink* sink) {
  DevLogSink* devSink = (DevLogSink*)sink;
  if (thread_atomic_sub_i32(&devSink->refCounter, 1) == 1) {
    alloc_free(g_allocHeap, mem_create(devSink->buffer, log_tracker_buffer_size));
    alloc_free_t(g_allocHeap, devSink);
  }
}

DevLogSink* dev_log_sink_create(void) {
  DevLogSink* sink = alloc_alloc_t(g_allocHeap, DevLogSink);

  *sink = (DevLogSink){
      .api    = {.write = dev_log_sink_write, .destroy = dev_log_sink_destroy},
      .buffer = alloc_alloc(g_allocHeap, log_tracker_buffer_size, alignof(DevLogEntry)).ptr,
  };

  return sink;
}

ecs_comp_define(DevLogTrackerComp) {
  bool         freeze;
  TimeDuration freezeTime;
  DevLogSink*  sink;
};

ecs_comp_define(DevLogViewerComp) {
  LogMask      mask;
  TimeZone     timezone;
  DevLogEntry* inspectEntry;
  UiPanel      inspectPanel;
  UiScrollview inspectScrollView;
};

static void ecs_destruct_log_tracker(void* data) {
  DevLogTrackerComp* comp = data;
  dev_log_sink_destroy((LogSink*)comp->sink);
}

ecs_view_define(LogGlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_write(DevLogTrackerComp);
}

ecs_view_define(LogViewerView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevLogViewerComp's are exclusively managed here.

  ecs_access_write(DevLogViewerComp);
  ecs_access_write(UiCanvasComp);
}

static DevLogTrackerComp*
dev_log_tracker_create(EcsWorld* world, const EcsEntityId entity, Logger* logger) {
  DevLogSink* sink = dev_log_sink_create();
  thread_atomic_store_i32(&sink->refCounter, 2); // Referenced by the logger and the viewer.
  log_add_sink(logger, (LogSink*)sink);
  return ecs_world_add_t(world, entity, DevLogTrackerComp, .sink = sink);
}

static String dev_log_inspect_title(const LogLevel lvl) {
  switch (lvl) {
  case LogLevel_Debug:
    return fmt_write_scratch("{} Dev", fmt_ui_shape(Article));
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

static UiColor dev_log_inspect_title_color(const LogLevel lvl) {
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

static void dev_log_inspect_param(UiCanvasComp* c, UiTable* t, const String k, const String v) {
  String parts[16];
  u32    partCount = array_elems(parts);
  string_split(v, '\n', parts, &partCount);

  for (u32 i = 0; i != partCount; ++i) {
    ui_table_next_row(c, t);
    ui_table_draw_row_bg(c, t, ui_color(48, 48, 48, 192));
    if (!i) {
      ui_label(c, k);
    }
    ui_table_next_column(c, t);
    ui_label(c, parts[i], .selectable = true);
  }
}

static f32 dev_log_inspect_desired_height(const DevLogEntry* entry) {
  UiTable table = ui_table(); // Dummy table.
  return ui_table_height(&table, 3 /* builtin params */ + entry->paramCount);
}

static void dev_log_inspect_open(DevLogViewerComp* viewer, DevLogEntry* entry) {
  const f32 heightDesired = dev_log_inspect_desired_height(entry);
  const f32 heightMax     = 600.0f;
  const f32 height        = math_min(heightDesired, heightMax);

  viewer->inspectEntry      = entry;
  viewer->inspectPanel      = ui_panel(.size = ui_vector(700, height));
  viewer->inspectScrollView = ui_scrollview();
}

static void dev_log_inspect_close(DevLogViewerComp* viewer) { viewer->inspectEntry = null; }

static void dev_log_inspect_draw(UiCanvasComp* c, DevLogViewerComp* viewer) {
  const DevLogEntry* entry = viewer->inspectEntry;

  ui_panel_begin(
      c,
      &viewer->inspectPanel,
      .title       = dev_log_inspect_title(entry->lvl),
      .topBarColor = dev_log_inspect_title_color(entry->lvl),
      .pinnable    = false);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const f32 totalHeight = ui_table_height(&table, 3 /* builtin params */ + entry->paramCount);
  ui_scrollview_begin(c, &viewer->inspectScrollView, UiLayer_Normal, totalHeight);

  DevLogStr* strItr = dev_log_msg(entry);
  dev_log_inspect_param(c, &table, string_lit("message"), dev_log_text(strItr));

  for (u32 paramIdx = 0; paramIdx != entry->paramCount; ++paramIdx) {
    DevLogStr* paramName = dev_log_str_next(strItr);
    DevLogStr* paramVal  = dev_log_str_next(paramName);
    dev_log_inspect_param(c, &table, dev_log_text(paramName), dev_log_text(paramVal));
    strItr = paramVal;
  }

  const FormatTimeTerms timeTerms = FormatTimeTerms_Time | FormatTimeTerms_Milliseconds;
  const String          timeVal   = fmt_write_scratch(
      "{}", fmt_time(entry->timestamp, .terms = timeTerms, .timezone = viewer->timezone));
  dev_log_inspect_param(c, &table, string_lit("time"), timeVal);

  const String fileName  = mem_create(entry->fileNamePtr, entry->fileNameLen);
  const String sourceVal = fmt_write_scratch("{}:{}", fmt_path(fileName), fmt_int(entry->line));
  dev_log_inspect_param(c, &table, string_lit("source"), sourceVal);

  ui_scrollview_end(c, &viewer->inspectScrollView);
  ui_panel_end(c, &viewer->inspectPanel);

  if (ui_panel_closed(&viewer->inspectPanel)) {
    dev_log_inspect_close(viewer); // Close when requested.
  }
  if (ui_canvas_input_any(c) && ui_canvas_status(c) == UiStatus_Idle) {
    dev_log_inspect_close(viewer); // Close when clicking outside the panel.
  }
}

static void dev_log_notif_tooltip(
    UiCanvasComp* c, const UiId id, const DevLogViewerComp* viewer, const DevLogEntry* entry) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, 4 * usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  DevLogStr* strItr = dev_log_msg(entry);
  fmt_write(&buffer, "\a.bmessage\ar: {}\n", fmt_text(dev_log_text(strItr)));

  for (u32 paramIdx = 0; paramIdx != entry->paramCount; ++paramIdx) {
    DevLogStr* paramName = dev_log_str_next(strItr);
    DevLogStr* paramVal  = dev_log_str_next(paramName);
    fmt_write(
        &buffer,
        "\a.b{}\ar: {}\n",
        fmt_text(dev_log_text(paramName)),
        fmt_text(dev_log_text(paramVal)));
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

static UiColor dev_log_notif_bg_color(const LogLevel lvl) {
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

static void dev_log_notif_draw_entry(
    UiCanvasComp* c, DevLogViewerComp* viewer, DevLogEntry* entry, const u32 repeat) {
  DevLogStr* msg = dev_log_msg(entry);

  ui_style_push(c);
  ui_style_color(c, dev_log_notif_bg_color(entry->lvl));
  const UiId bgId = ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_Interactable);
  ui_style_pop(c);

  ui_layout_push(c);
  ui_layout_grow(c, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);

  String text;
  if (repeat) {
    text = fmt_write_scratch("x{} {}", fmt_int(repeat + 1), fmt_text(dev_log_text(msg)));
  } else {
    text = dev_log_text(msg);
  }
  ui_canvas_draw_text(c, text, 15, UiAlign_MiddleLeft, UiFlags_None);
  ui_layout_pop(c);

  const UiStatus status = ui_canvas_elem_status(c, bgId);
  if (status == UiStatus_Activated) {
    if (repeat) {
      entry->flags &= ~DevLogFlags_Combine;
    } else {
      dev_log_inspect_open(viewer, entry);
    }
    ui_canvas_sound(c, UiSoundType_Click);
  }
  if (status >= UiStatus_Hovered) {
    dev_log_notif_tooltip(c, bgId, viewer, entry);
  } else {
    ui_canvas_id_skip(c, 2); // NOTE: Tooltips consume two ids.
  }
}

static void
dev_log_notif_draw(UiCanvasComp* c, const DevLogTrackerComp* tracker, DevLogViewerComp* viewer) {
  ui_layout_move_to(c, UiBase_Container, UiAlign_TopRight, Ui_XY);
  ui_layout_resize(c, UiAlign_TopRight, ui_vector(400, 0), UiBase_Absolute, Ui_X);
  ui_layout_resize(c, UiAlign_TopLeft, ui_vector(0, 20), UiBase_Absolute, Ui_Y);

  ui_style_push(c);
  ui_style_outline(c, 0);

  /**
   * Because 'dev_log_sink_write' only adds new entries (but never removes) and this is never
   * called in parallel with 'dev_log_prune_older' we can avoid taking the spinlock and instead
   * iterate up to the last fully written one.
   */
  thread_atomic_fence_acquire();
  DevLogEntry* first = tracker->sink->entryHead;
  DevLogEntry* last  = tracker->sink->entryTail;
  if (!first) {
    goto End; // Buffer is emtpy.
  }
  for (DevLogEntry* itr = first;; itr = dev_log_next(tracker->sink, itr)) {
    if (viewer->mask & (1 << itr->lvl)) {
      DevLogEntry* entry  = itr;
      u32          repeat = 0;
      if (entry->flags & DevLogFlags_Combine) {
        for (;;) {
          if (itr == last) {
            break;
          }
          DevLogEntry* next = dev_log_next(tracker->sink, itr);
          if (dev_log_is_dup(entry, next)) {
            itr = next;
            ++repeat;
          } else {
            break;
          }
        }
      }
      dev_log_notif_draw_entry(c, viewer, entry, repeat);
      ui_layout_next(c, Ui_Down, 0);
    }
    if (itr == last) {
      break; // Reached the last written one when we synchronized with dev_log_sink_write.
    }
  }
End:
  ui_style_pop(c);
}

ecs_system_define(DevLogUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, LogGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  DevLogTrackerComp*   tracker = ecs_view_write_t(globalItr, DevLogTrackerComp);
  const SceneTimeComp* time    = ecs_view_read_t(globalItr, SceneTimeComp);
  if (tracker->freeze) {
    tracker->freezeTime += time->realDelta;
  }
  const TimeReal now          = time_real_clock();
  const TimeReal oldestToKeep = time_real_offset(now, -(tracker->freezeTime + log_tracker_max_age));
  dev_log_prune_older(tracker->sink, oldestToKeep);

  tracker->freeze   = false;
  EcsView* drawView = ecs_world_view_t(world, LogViewerView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    DevLogViewerComp* viewer = ecs_view_write_t(itr, DevLogViewerComp);
    UiCanvasComp*     canvas = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    ui_canvas_to_front(canvas); // Always draw logs on-top.

    // Draw notifications (new log entries).
    const UiId notifIdFirst = ui_canvas_id_peek(canvas);
    dev_log_notif_draw(canvas, tracker, viewer);
    const UiId notifIdLast = ui_canvas_id_peek(canvas) - 1;
    if (ui_canvas_group_status(canvas, notifIdFirst, notifIdLast) >= UiStatus_Hovered) {
      tracker->freeze = true; // Don't remove entries while hovering any of the notifications.
    }

    // Draw inspector.
    if (viewer->inspectEntry) {
      tracker->freeze = true; // Don't remove entries while inspecting.
      dev_log_inspect_draw(canvas, viewer);
    }
  }
}

ecs_module_init(dev_log_viewer_module) {
  ecs_register_comp(DevLogTrackerComp, .destructor = ecs_destruct_log_tracker);
  ecs_register_comp(DevLogViewerComp);

  ecs_register_view(LogGlobalView);
  ecs_register_view(LogViewerView);

  ecs_register_system(DevLogUpdateSys, ecs_view_id(LogGlobalView), ecs_view_id(LogViewerView));
}

void dev_log_tracker_init(EcsWorld* world, Logger* logger) {
  dev_log_tracker_create(world, ecs_world_global(world), logger);
}

EcsEntityId dev_log_viewer_create(EcsWorld* world, const EcsEntityId window, const LogMask mask) {
  const EcsEntityId viewerEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, viewerEntity, DevLogViewerComp, .mask = mask, .timezone = time_zone_current());
  return viewerEntity;
}

void dev_log_viewer_set_mask(DevLogViewerComp* viewer, const LogMask mask) { viewer->mask = mask; }
