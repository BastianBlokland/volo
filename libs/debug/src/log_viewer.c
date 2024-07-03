#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs_world.h"
#include "log.h"
#include "log_logger.h"
#include "log_sink.h"
#include "ui.h"

#define log_tracker_mask (LogMask_Info | LogMask_Warn | LogMask_Error)
#define log_tracker_buffer_size (8 * usize_kibibyte)
#define log_tracker_max_message_size 64
#define log_tracker_max_age time_seconds(10)

ASSERT(log_tracker_max_message_size < u8_max, "Message length has to be storable in a 8 bits")

typedef struct sDebugLogMessage DebugLogMessage;

struct sDebugLogMessage {
  DebugLogMessage* next;
  TimeReal         timestamp;
  LogLevel         lvl : 8;
  u8               length;
  u16              line;
  u32              counter;
  String           file;
  u8               data[log_tracker_max_message_size];
};

/**
 * Sink that will receive logger messages.
 * NOTE: Needs a stable pointer as it will be registered to the logger.
 * NOTE: Sink needs to stay alive as long as the logger still exists or the tracker still exists,
 * to achieve this it has a basic ref-counter.
 */
typedef struct {
  LogSink          api;
  i32              refCounter;
  ThreadSpinLock   msgLock;
  u8*              msgBuffer;    // Circular buffer of (dynamically sized) messages.
  u8*              msgBufferPos; // Current write position.
  DebugLogMessage* msgHead;
  DebugLogMessage* msgTail;
} DebugLogSink;

static bool debug_log_msg_is_dup(const DebugLogMessage* msg, const String newMsgText) {
  return msg->length == newMsgText.size && mem_eq(mem_create(msg->data, msg->length), newMsgText);
}

static bool debug_log_msg_free_until(DebugLogSink* debugSink, const u8* endPos) {
  if (!debugSink->msgHead) {
    return true; // No messages active; all are free.
  }
  if (endPos <= (u8*)debugSink->msgHead) {
    return true;
  }
  if (debugSink->msgTail >= debugSink->msgHead && endPos > (u8*)debugSink->msgTail) {
    return true;
  }
  return false; // Pos overlaps with the message range.
}

static void debug_log_sink_write(
    LogSink*        sink,
    const LogLevel  lvl,
    const SourceLoc srcLoc,
    const TimeReal  timestamp,
    const String    message,
    const LogParam* params) {
  (void)params;
  DebugLogSink* debugSink = (DebugLogSink*)sink;
  if ((log_tracker_mask & (1 << lvl)) == 0) {
    return;
  }
  thread_spinlock_lock(&debugSink->msgLock);
  {
    bool duplicate = false;
    if (debugSink->msgTail && debug_log_msg_is_dup(debugSink->msgTail, message)) {
      debugSink->msgTail->timestamp = timestamp;
      ++debugSink->msgTail->counter;
      duplicate = true;
    }

    if (!duplicate) {
      u8* nextBufferPos = debugSink->msgBufferPos + sizeof(DebugLogMessage);
      if (nextBufferPos >= (debugSink->msgBuffer + log_tracker_buffer_size)) {
        nextBufferPos = debugSink->msgBuffer; // Wrap around to the beginning.
      }
      diag_assert(bits_aligned_ptr(nextBufferPos, alignof(DebugLogMessage)));

      // Check if we have space for a new message, if not: drop the message.
      if (debug_log_msg_free_until(debugSink, nextBufferPos)) {
        DebugLogMessage* newMsg = (DebugLogMessage*)debugSink->msgBufferPos;
        newMsg->next            = null;
        newMsg->timestamp       = timestamp;
        newMsg->lvl             = lvl;
        newMsg->length          = math_min((u8)message.size, log_tracker_max_message_size);
        newMsg->counter         = 1;
        newMsg->line            = (u16)math_min(srcLoc.line, u16_max);
        newMsg->file            = srcLoc.file;
        mem_cpy(mem_create(newMsg->data, newMsg->length), string_slice(message, 0, newMsg->length));

        if (debugSink->msgTail) {
          debugSink->msgTail->next = newMsg;
        } else {
          debugSink->msgHead = newMsg;
        }
        debugSink->msgTail      = newMsg;
        debugSink->msgBufferPos = nextBufferPos;
      }
    }
  }
  thread_spinlock_unlock(&debugSink->msgLock);
}

static void debug_log_sink_destroy(LogSink* sink) {
  DebugLogSink* debugSink = (DebugLogSink*)sink;
  if (thread_atomic_sub_i32(&debugSink->refCounter, 1) == 1) {
    alloc_free(g_allocHeap, mem_create(debugSink->msgBuffer, log_tracker_buffer_size));
    alloc_free_t(g_allocHeap, debugSink);
  }
}

static void debug_log_sink_prune_older(DebugLogSink* s, const TimeReal timestamp) {
  thread_spinlock_lock(&s->msgLock);
  {
    for (; s->msgHead && s->msgHead->timestamp < timestamp; s->msgHead = s->msgHead->next)
      ;
    if (!s->msgHead) {
      s->msgTail = null;
    }
  }
  thread_spinlock_unlock(&s->msgLock);
}

DebugLogSink* debug_log_sink_create(void) {
  DebugLogSink* sink = alloc_alloc_t(g_allocHeap, DebugLogSink);

  u8* msgBuffer = alloc_alloc(g_allocHeap, log_tracker_buffer_size, alignof(DebugLogMessage)).ptr;

  *sink = (DebugLogSink){
      .api          = {.write = debug_log_sink_write, .destroy = debug_log_sink_destroy},
      .msgBuffer    = msgBuffer,
      .msgBufferPos = msgBuffer,
  };

  return sink;
}

ecs_comp_define(DebugLogTrackerComp) { DebugLogSink* sink; };
ecs_comp_define(DebugLogViewerComp) { LogMask mask; };

static void ecs_destruct_log_tracker(void* data) {
  DebugLogTrackerComp* comp = data;
  debug_log_sink_destroy((LogSink*)comp->sink);
}

ecs_view_define(LogTrackerGlobalView) { ecs_access_write(DebugLogTrackerComp); }

ecs_view_define(LogViewerDrawView) {
  ecs_access_read(DebugLogViewerComp);
  ecs_access_write(UiCanvasComp);
}

static DebugLogTrackerComp* debug_log_tracker_global(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, LogTrackerGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, DebugLogTrackerComp) : null;
}

static DebugLogTrackerComp*
debug_log_tracker_create(EcsWorld* world, const EcsEntityId entity, Logger* logger) {
  DebugLogSink* sink = debug_log_sink_create();
  thread_atomic_store_i32(&sink->refCounter, 2); // Referenced by the logger and the viewer.
  log_add_sink(logger, (LogSink*)sink);
  return ecs_world_add_t(world, entity, DebugLogTrackerComp, .sink = sink);
}

ecs_system_define(DebugLogUpdateSys) {
  DebugLogTrackerComp* trackerGlobal = debug_log_tracker_global(world);
  if (!trackerGlobal) {
    debug_log_tracker_create(world, ecs_world_global(world), g_logger);
    return;
  }
  const TimeReal oldestToKeep = time_real_offset(time_real_clock(), -log_tracker_max_age);
  debug_log_sink_prune_older(trackerGlobal->sink, oldestToKeep);
}

static UiColor debug_log_bg_color(const LogLevel lvl) {
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

static void debug_log_draw_message(UiCanvasComp* canvas, const DebugLogMessage* msg) {
  const String str = mem_create(msg->data, msg->length);

  ui_style_push(canvas);
  ui_style_color(canvas, debug_log_bg_color(msg->lvl));
  const UiId bgId = ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_Interactable);
  ui_style_pop(canvas);

  ui_layout_push(canvas);
  ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);

  String text;
  if (msg->counter > 1) {
    text = fmt_write_scratch("x{} {}", fmt_int(msg->counter), fmt_text(str));
  } else {
    text = str;
  }
  ui_canvas_draw_text(canvas, text, 15, UiAlign_MiddleLeft, UiFlags_None);
  ui_layout_pop(canvas);

  ui_tooltip(canvas, bgId, fmt_write_scratch("{}:{}", fmt_path(msg->file), fmt_int(msg->line)));
}

static void debug_log_draw_messages(
    UiCanvasComp* canvas, const DebugLogTrackerComp* tracker, const LogMask mask) {
  ui_layout_move_to(canvas, UiBase_Container, UiAlign_TopRight, Ui_XY);
  ui_layout_resize(canvas, UiAlign_TopRight, ui_vector(500, 0), UiBase_Absolute, Ui_X);
  ui_layout_resize(canvas, UiAlign_TopLeft, ui_vector(0, 20), UiBase_Absolute, Ui_Y);

  ui_style_outline(canvas, 0);

  thread_spinlock_lock(&tracker->sink->msgLock);
  {
    for (DebugLogMessage* msg = tracker->sink->msgHead; msg; msg = msg->next) {
      if (mask & (1 << msg->lvl)) {
        debug_log_draw_message(canvas, msg);
        ui_layout_next(canvas, Ui_Down, 0);
      }
    }
  }
  thread_spinlock_unlock(&tracker->sink->msgLock);
}

ecs_system_define(DebugLogDrawSys) {
  DebugLogTrackerComp* trackerGlobal = debug_log_tracker_global(world);
  if (!trackerGlobal) {
    return;
  }

  EcsView* drawView = ecs_world_view_t(world, LogViewerDrawView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    const DebugLogViewerComp* viewer = ecs_view_read_t(itr, DebugLogViewerComp);
    UiCanvasComp*             canvas = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    ui_canvas_to_front(canvas); // Always draw logs on-top.
    debug_log_draw_messages(canvas, trackerGlobal, viewer->mask);
  }
}

ecs_module_init(debug_log_viewer_module) {
  ecs_register_comp(DebugLogTrackerComp, .destructor = ecs_destruct_log_tracker);
  ecs_register_comp(DebugLogViewerComp);

  ecs_register_view(LogTrackerGlobalView);
  ecs_register_view(LogViewerDrawView);

  ecs_register_system(DebugLogUpdateSys, ecs_view_id(LogTrackerGlobalView));
  ecs_register_system(
      DebugLogDrawSys, ecs_view_id(LogTrackerGlobalView), ecs_view_id(LogViewerDrawView));
}

EcsEntityId debug_log_viewer_create(EcsWorld* world, const EcsEntityId window, const LogMask mask) {
  const EcsEntityId viewerEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToBack);
  ecs_world_add_t(world, viewerEntity, DebugLogViewerComp, .mask = mask);
  return viewerEntity;
}

void debug_log_viewer_set_mask(DebugLogViewerComp* viewer, const LogMask mask) {
  viewer->mask = mask;
}
