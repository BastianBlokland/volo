#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "log_logger.h"
#include "trace_sink.h"
#include "trace_sink_superluminal.h"

#include "event_internal.h"

/**
 * Log sink implementation that uses the Superluminal PerformanceAPI.
 * Documentation: https://www.superluminal.eu/docs/documentation.html#using_performance_api
 */

#define trace_sl_version_major 3
#define trace_sl_version_minor 0
#define trace_sl_version ((trace_sl_version_major << 16) | trace_sl_version_minor)

/**
 * Default Superluminal installation path.
 * TODO: Make this configurable.
 */
static const String g_traceSlPathDefault =
    string_static("C:/Program Files/Superluminal/Performance/API/dll/x64/PerformanceAPI.dll");

// NOTE: This struct needs to match the 'PerformanceAPI_SuppressTailCallOptimization'.
typedef struct {
  i64 suppressTailCall[3]; // Used to suppress tail-call optimization.
} SuperluminalFunctionRet;

// NOTE: This struct needs to match the 'PerformanceAPI_Functions'.
typedef struct {
  // clang-format off
  uptr                              setCurrentThreadName;
  uptr                              setCurrentThreadNameN;
  uptr                              beginEvent;
  void                   (SYS_DECL* beginEventN)(const char* id, u16 idLen, const char* data, u16 dataLen, u32 color);
  uptr                              beginEventWide;
  uptr                              beginEventWideN;
  SuperluminalFunctionRet(SYS_DECL* endEvent)(void);
  uptr                              registerFiber;
  uptr                              unregisterFiber;
  uptr                              beginFiberSwitch;
  uptr                              endFiberSwitch;
  // clang-format on
} SuperluminalFunctions;

typedef struct {
  TraceSink             api;
  Allocator*            alloc;
  DynLib*               slLib;
  SuperluminalFunctions slFunctions;
} TraceSinkSl;

static bool trace_sink_sl_init(TraceSinkSl* sink) {
#if defined(VOLO_WIN32)
  DynLibResult loadRes = dynlib_load(sink->alloc, g_traceSlPathDefault, &sink->slLib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_d(
        "Failed to load Superluminal library",
        log_param("err", fmt_text(err)),
        log_param("search-path", fmt_path(g_traceSlPathDefault)));
    return false;
  }

  log_i("Superluminal library loaded", log_param("path", fmt_path(dynlib_path(sink->slLib))));

  int(SYS_DECL * getApiFunc)(int version, SuperluminalFunctions* out);
  getApiFunc = dynlib_symbol(sink->slLib, string_lit("PerformanceAPI_GetAPI"));
  if (!getApiFunc) {
    log_w("Failed to load the 'PerformanceAPI_GetAPI' Superluminal symbol");
    return false;
  }
  if (!getApiFunc(trace_sl_version, &sink->slFunctions)) {
    log_w("Superluminal trace sink initialized failed");
    return false;
  }
  return true;
#else
  (void)sink;
  return false;
#endif
}

static u32 trace_sink_sl_color(const TraceColor color) {
  switch (color) {
  case TraceColor_Default:
  case TraceColor_White:
    return 0xFFFFFFFF;
  case TraceColor_Red:
    return 0xFF9090FF;
  case TraceColor_Green:
    return 0x90FF90FF;
  case TraceColor_Blue:
    return 0x9090FFFF;
  }
  diag_crash_msg("Unsupported TraceColor value");
}

static void trace_sink_sl_event_begin(
    TraceSink* sink, const String id, const TraceColor color, const String msg) {
  TraceSinkSl* slSink = (TraceSinkSl*)sink;
  if (slSink->slFunctions.beginEventN) {
    const u32 rgba = trace_sink_sl_color(color);
    /**
     * NOTE: Superluminal also uses utf8 encoded strings, BUT unfortunately they specify the size as
     * unicode characters instead of bytes. This means to support non-ascii we need to walk the
     * strings to count the utf8 characters. At the moment we just don't support non-ascii.
     */
    slSink->slFunctions.beginEventN(id.ptr, (u16)id.size, msg.ptr, (u16)msg.size, rgba);
  }
}

static void trace_sink_sl_event_end(TraceSink* sink) {
  TraceSinkSl* slSink = (TraceSinkSl*)sink;
  if (slSink->slFunctions.endEvent) {
    slSink->slFunctions.endEvent();
  }
}

static void trace_sink_sl_destroy(TraceSink* sink) {
  TraceSinkSl* slSink = (TraceSinkSl*)sink;
  if (slSink->slLib) {
    dynlib_destroy(slSink->slLib);
  }
  alloc_free_t(slSink->alloc, slSink);
}

TraceSink* trace_sink_superluminal(Allocator* alloc) {
  TraceSinkSl* sink = alloc_alloc_t(alloc, TraceSinkSl);

  *sink = (TraceSinkSl){
      .api =
          {
              .eventBegin = trace_sink_sl_event_begin,
              .eventEnd   = trace_sink_sl_event_end,
              .destroy    = trace_sink_sl_destroy,
          },
      .alloc = alloc,
  };

  trace_sink_sl_init(sink);

  return (TraceSink*)sink;
}
