#include "core_alloc.h"
#include "core_array.h"
#include "core_dynlib.h"
#include "core_file.h"
#include "core_float.h"
#include "core_format.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "data_registry.h"
#include "debug_stats.h"
#include "ecs_runner.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "geo_query.h"
#include "rend_settings.h"
#include "rend_stats.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_nav.h"
#include "scene_time.h"
#include "ui.h"
#include "ui_stats.h"
#include "vfx_stats.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

static const f32 g_statsLabelWidth       = 210;
static const u8  g_statsBgAlpha          = 150;
static const u8  g_statsSectionBgAlpha   = 200;
static const f32 g_statsInvAverageWindow = 1.0f / 10.0f;

static const UiColor g_statsChartColors[] = {
    {0, 128, 128, 255},
    {0, 0, 128, 255},
    {128, 128, 0, 255},
    {128, 0, 0, 255},
    {128, 0, 128, 255},
    {128, 128, 0, 255},
    {0, 128, 0, 255},
    {255, 0, 255, 255},
    {0, 0, 255, 255},
    {128, 0, 0, 255},
};

#define stats_plot_size 128
#define stats_notify_max_key_size 32
#define stats_notify_max_value_size 16
#define stats_notify_max_age time_seconds(3)

typedef enum {
  DebugBgFlags_None    = 0,
  DebugBgFlags_Section = 1 << 0,
} DebugBgFlags;

typedef struct {
  ALIGNAS(16) f32 values[stats_plot_size];
  u32  cur;
  bool initialized;
} DebugStatPlot;

typedef struct {
  TimeReal timestamp;
  u8       keyLength, valueLength;
  u8       key[stats_notify_max_key_size];
  u8       value[stats_notify_max_value_size];
} DebugStatsNotification;

ecs_comp_define(DebugStatsComp) {
  DebugStatShow show;
  EcsEntityId   canvas;

  DebugStatPlot* frameDurPlot; // In microseconds.
  TimeDuration   frameDurDesired;
  DebugStatPlot* gpuExecDurPlot; // In microseconds.

  u32           inspectPassIndex; // Pass to show stats for.
  SceneNavLayer inspectNavLayer;  // Navigation layer to show stats for.

  // Cpu frame fractions.
  f32 rendWaitForGpuFrac, rendPresAcqFrac, rendPresEnqFrac, rendPresWaitFrac, rendLimiterFrac;

  // Gpu frame fractions.
  f32 gpuWaitFrac, gpuExecFrac;
  f32 gpuPassFrac[rend_stats_max_passes];
};

ecs_comp_define(DebugStatsGlobalComp) {
  DynArray notifications; // DebugStatsNotification[].

  u64   allocPrevPageCounter, allocPrevHeapCounter, allocPrevPersistCounter;
  u32   fileCount, dynlibCount;
  usize fileMappingSize;
  u32   globalStringCount;

  DebugStatPlot* ecsFlushDurPlot; // In microseconds.
};

static void ecs_destruct_stats(void* data) {
  DebugStatsComp* comp = data;
  alloc_free_t(g_allocHeap, comp->frameDurPlot);
  alloc_free_t(g_allocHeap, comp->gpuExecDurPlot);
}

static void ecs_destruct_stats_global(void* data) {
  DebugStatsGlobalComp* comp = data;
  dynarray_destroy(&comp->notifications);
  alloc_free_t(g_allocHeap, comp->ecsFlushDurPlot);
}

static DebugStatsNotification* debug_notify_get(DebugStatsGlobalComp* comp, const String key) {
  // Find an existing notification with the same key.
  dynarray_for_t(&comp->notifications, DebugStatsNotification, notif) {
    if (string_eq(mem_create(notif->key, notif->keyLength), key)) {
      return notif;
    }
  }

  // If none was found; create a new notification for this key.
  DebugStatsNotification* notif = dynarray_push_t(&comp->notifications, DebugStatsNotification);
  notif->keyLength              = math_min((u8)key.size, stats_notify_max_key_size);
  mem_cpy(mem_create(notif->key, notif->keyLength), string_slice(key, 0, notif->keyLength));
  return notif;
}

static void debug_notify_prune_older(DebugStatsGlobalComp* comp, const TimeReal timestamp) {
  for (usize i = comp->notifications.size; i-- > 0;) {
    DebugStatsNotification* notif = dynarray_at_t(&comp->notifications, i, DebugStatsNotification);
    if (notif->timestamp < timestamp) {
      dynarray_remove(&comp->notifications, i, 1);
    }
  }
}

static DebugStatPlot* debug_plot_alloc(Allocator* alloc) {
  DebugStatPlot* plot = alloc_alloc_t(alloc, DebugStatPlot);
  mem_set(mem_create(plot, sizeof(DebugStatPlot)), 0);
  return plot;
}

static void debug_plot_set(DebugStatPlot* plot, const f32 value) {
#ifdef VOLO_SIMD
  ASSERT((stats_plot_size % 4) == 0, "Only multiple of 4 plot sizes are supported");
  const SimdVec valueVec = simd_vec_broadcast(value);
  for (u32 i = 0; i != stats_plot_size; i += 4) {
    simd_vec_store(valueVec, plot->values + i);
  }
#else
  for (u32 i = 0; i != stats_plot_size; ++i) {
    plot->values[i] = value;
  }
#endif
}

static void debug_plot_add(DebugStatPlot* plot, const f32 value) {
  if (UNLIKELY(!plot->initialized)) {
    debug_plot_set(plot, value);
    plot->initialized = true;
  }
  plot->values[plot->cur] = value;
  plot->cur               = (plot->cur + 1) % stats_plot_size;
}

static void debug_plot_add_dur(DebugStatPlot* plot, const TimeDuration value) {
  debug_plot_add(plot, (f32)(value / (f64)time_microsecond));
}

static f32 debug_plot_newest(const DebugStatPlot* plot) {
  const u32 newestIndex = (plot->cur + stats_plot_size - 1) % stats_plot_size;
  return plot->values[newestIndex];
}

static f32 debug_plot_min(const DebugStatPlot* plot) {
#ifdef VOLO_SIMD
  ASSERT((stats_plot_size % 4) == 0, "Only multiple of 4 plot sizes are supported");
  SimdVec min = simd_vec_broadcast(plot->values[0]);
  for (u32 i = 0; i != stats_plot_size; i += 4) {
    min = simd_vec_min(min, simd_vec_min_comp(simd_vec_load(plot->values + i)));
  }
  return simd_vec_x(min);
#else
  f32 min = plot->values[0];
  for (u32 i = 1; i != stats_plot_size; ++i) {
    if (plot->values[i] < min) {
      min = plot->values[i];
    }
  }
  return min;
#endif
}

static f32 debug_plot_max(const DebugStatPlot* plot) {
#ifdef VOLO_SIMD
  ASSERT((stats_plot_size % 4) == 0, "Only multiple of 4 plot sizes are supported");
  SimdVec max = simd_vec_broadcast(plot->values[0]);
  for (u32 i = 0; i != stats_plot_size; i += 4) {
    max = simd_vec_max(max, simd_vec_max_comp(simd_vec_load(plot->values + i)));
  }
  return simd_vec_x(max);
#else
  f32 max = plot->values[0];
  for (u32 i = 1; i != stats_plot_size; ++i) {
    if (plot->values[i] > max) {
      max = plot->values[i];
    }
  }
  return max;
#endif
}

static f32 debug_plot_var(const DebugStatPlot* plot) {
  return debug_plot_max(plot) - debug_plot_min(plot);
}

static f32 debug_plot_sum(const DebugStatPlot* plot) {
#ifdef VOLO_SIMD
  ASSERT((stats_plot_size % 4) == 0, "Only multiple of 4 plot sizes are supported");
  SimdVec accum = simd_vec_zero();
  for (u32 i = 0; i != stats_plot_size; i += 4) {
    accum = simd_vec_add(accum, simd_vec_add_comp(simd_vec_load(plot->values + i)));
  }
  return simd_vec_x(accum);
#else
  f32 sum = plot->values[0];
  for (u32 i = 1; i != stats_plot_size; ++i) {
    sum += plot->values[i];
  }
  return sum;
#endif
}

static f32 debug_plot_avg(const DebugStatPlot* plot) {
  return debug_plot_sum(plot) / (f32)stats_plot_size;
}

static TimeDuration debug_plot_max_dur(const DebugStatPlot* plot) {
  return (TimeDuration)(debug_plot_max(plot) * (f64)time_microsecond);
}

static TimeDuration debug_plot_var_dur(const DebugStatPlot* plot) {
  return (TimeDuration)(debug_plot_var(plot) * (f64)time_microsecond);
}

static TimeDuration debug_plot_avg_dur(const DebugStatPlot* plot) {
  return (TimeDuration)(debug_plot_avg(plot) * (f64)time_microsecond);
}

static void debug_avg_f32(f32* value, const f32 new) {
  *value += (new - *value) * g_statsInvAverageWindow;
}

static f32 debug_frame_frac(const TimeDuration whole, const TimeDuration part) {
  return math_clamp_f32(part / (f32)whole, 0, 1);
}

static void stats_draw_bg(UiCanvasComp* c, const DebugBgFlags flags) {
  ui_style_push(c);
  const u8 alpha = flags & DebugBgFlags_Section ? g_statsSectionBgAlpha : g_statsBgAlpha;
  ui_style_color(c, ui_color(0, 0, 0, alpha));
  ui_canvas_draw_glyph(c, UiShape_Square, 0, UiFlags_None);
  ui_style_pop(c);
}

static void stats_draw_label(UiCanvasComp* c, const String label) {
  ui_layout_push(c);

  ui_layout_resize(c, UiAlign_BottomLeft, ui_vector(g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);
  ui_layout_grow(c, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);
  ui_label(c, label, .align = UiAlign_MiddleLeft);

  ui_layout_pop(c);
}

static void stats_draw_value(UiCanvasComp* c, const String value) {
  ui_layout_push(c);
  ui_style_push(c);

  ui_layout_grow(c, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

  ui_style_variation(c, UiVariation_Monospace);
  ui_style_weight(c, UiWeight_Bold);
  ui_label(c, value, .selectable = true);

  ui_style_pop(c);
  ui_layout_pop(c);
}

static bool stats_draw_button(UiCanvasComp* c, const String value) {
  ui_layout_push(c);
  ui_style_push(c);

  ui_layout_grow(c, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

  const bool pressed = ui_button(c, .label = value, .frameColor = ui_color(24, 24, 24, 128));

  ui_style_pop(c);
  ui_layout_pop(c);
  return pressed;
}

static void stats_draw_val_entry(UiCanvasComp* c, const String label, const String value) {
  stats_draw_bg(c, DebugBgFlags_None);
  stats_draw_label(c, label);
  stats_draw_value(c, value);
  ui_layout_next(c, Ui_Down, 0);
}

static bool stats_draw_button_entry(UiCanvasComp* c, const String label, const String value) {
  stats_draw_bg(c, DebugBgFlags_None);
  stats_draw_label(c, label);
  const bool pressed = stats_draw_button(c, value);
  ui_layout_next(c, Ui_Down, 0);
  return pressed;
}

static bool stats_draw_section(UiCanvasComp* c, const String label) {
  ui_canvas_id_block_next(c);
  stats_draw_bg(c, DebugBgFlags_Section);
  const bool isOpen = ui_section(c, .label = label);
  ui_layout_next(c, Ui_Down, 0);
  return isOpen;
}

typedef void (*PlotValueWriter)(DynString*, f32 value);

static void stats_draw_plot_tooltip(
    UiCanvasComp* c, const DebugStatPlot* plot, const PlotValueWriter valWriter) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

#define APPEND_PLOT_VAL(_TITLE_, _FUNC_)                                                           \
  do {                                                                                             \
    dynstring_append(&buffer, string_lit("\a.b" _TITLE_ "\ar:\a>09"));                             \
    valWriter(&buffer, _FUNC_(plot));                                                              \
    dynstring_append_char(&buffer, '\n');                                                          \
  } while (false)

  if (plot->initialized) {
    APPEND_PLOT_VAL("Newest", debug_plot_newest);
    APPEND_PLOT_VAL("Average", debug_plot_avg);
    APPEND_PLOT_VAL("Min", debug_plot_min);
    APPEND_PLOT_VAL("Max", debug_plot_max);
    APPEND_PLOT_VAL("Variance", debug_plot_var);
  }

  const UiId id = ui_canvas_id_peek(c);
  ui_canvas_draw_glyph(c, UiShape_Empty, 0, UiFlags_Interactable); // Invisible rect.
  ui_tooltip(c, id, dynstring_view(&buffer), .variation = UiVariation_Monospace);
}

static void stats_draw_plot(
    UiCanvasComp*         c,
    const DebugStatPlot*  plot,
    const f32             minVal,
    const f32             maxVal,
    const PlotValueWriter valWriter) {
  static const f32 g_stepX    = 1.0f / stats_plot_size;
  static const f32 g_statRows = 2.0f; // Amount of rows the plot takes up.

  ui_layout_push(c);
  ui_layout_move_dir(c, Ui_Down, g_statRows - 1.0f, UiBase_Current);
  ui_layout_resize(c, UiAlign_BottomLeft, ui_vector(0, g_statRows), UiBase_Current, Ui_Y);
  ui_layout_container_push(c, UiClip_None);

  // Draw background.
  stats_draw_bg(c, DebugBgFlags_None);

  ui_style_push(c);
  ui_style_outline(c, 0);

  // Draw center line.
  ui_style_color(c, ui_color(128, 128, 128, 128));
  ui_layout_move_to(c, UiBase_Container, UiAlign_MiddleCenter, Ui_Y);
  ui_layout_resize(c, UiAlign_MiddleCenter, ui_vector(0, 2), UiBase_Absolute, Ui_Y);
  ui_canvas_draw_glyph(c, UiShape_Square, 0, UiFlags_None);

  // Draw values.
  const u32 newestIndex = (plot->cur + stats_plot_size - 1) % stats_plot_size;
  f32       x           = 0.0f;
  for (u32 i = 0; i != stats_plot_size; ++i, x += g_stepX) {
    const f32 value   = plot->values[i];
    const f32 yCenter = math_clamp_f32(math_unlerp(minVal, maxVal, value), 0.0f, 1.0f);

    const bool    isNewest = i == newestIndex;
    const UiColor color    = isNewest ? ui_color_yellow : ui_color(255, 255, 255, 178);
    const f32     height   = isNewest ? 4.0f : 2.0f;

    ui_style_color(c, color);

    ui_layout_set_pos(c, UiBase_Container, ui_vector(x, yCenter), UiBase_Container);
    ui_layout_resize(c, UiAlign_MiddleLeft, ui_vector(g_stepX, 0), UiBase_Container, Ui_X);
    ui_layout_resize(c, UiAlign_MiddleCenter, ui_vector(0, height), UiBase_Absolute, Ui_Y);

    ui_canvas_draw_glyph(c, UiShape_Square, 0, UiFlags_None);
  }

  ui_layout_inner(c, UiBase_Container, UiAlign_BottomLeft, ui_vector(1, 1), UiBase_Container);
  stats_draw_plot_tooltip(c, plot, valWriter);

  ui_style_pop(c);
  ui_layout_container_pop(c);
  ui_layout_pop(c);
  ui_layout_move_dir(c, Ui_Down, g_statRows, UiBase_Current);
}

static void stats_dur_val_writer(DynString* str, const f32 value) {
  const TimeDuration valueDur = (TimeDuration)(value * (f64)time_microsecond);
  fmt_write(str, "{>8}", fmt_duration(valueDur, .minDecDigits = 1, .maxDecDigits = 1));
}

static void stats_draw_plot_dur(
    UiCanvasComp* c, const DebugStatPlot* plot, const TimeDuration min, const TimeDuration max) {
  const f32 minUs = (f32)(min / (f64)time_microsecond);
  const f32 maxUs = (f32)(max / (f64)time_microsecond);
  stats_draw_plot(c, plot, minUs, maxUs, stats_dur_val_writer);
}

static void stats_draw_frametime(UiCanvasComp* c, const DebugStatsComp* stats) {
  const f64 g_errorThreshold = 1.25;
  const f64 g_warnThreshold  = 1.025;

  const TimeDuration durAvg      = debug_plot_avg_dur(stats->frameDurPlot);
  const TimeDuration durVariance = debug_plot_var_dur(stats->frameDurPlot);

  String colorText = string_empty;
  if (durAvg > stats->frameDurDesired * g_errorThreshold) {
    colorText = ui_escape_color_scratch(ui_color_red);
  } else if (durAvg > stats->frameDurDesired * g_warnThreshold) {
    colorText = ui_escape_color_scratch(ui_color_yellow);
  }

  const f32    freq = 1.0f / (durAvg / (f32)time_second);
  const String freqText =
      fmt_write_scratch("{}hz", fmt_float(freq, .minDecDigits = 1, .maxDecDigits = 1));

  stats_draw_val_entry(
      c,
      string_lit("Frame time"),
      fmt_write_scratch(
          "{}{<8}{<8}{>7} var",
          fmt_text(colorText),
          fmt_duration(durAvg, .minDecDigits = 1),
          fmt_text(freqText),
          fmt_duration(durVariance, .maxDecDigits = 0)));
}

typedef struct {
  f32     frac;
  UiColor color;
} StatChartEntry;

static void stats_draw_chart(
    UiCanvasComp* c, const StatChartEntry* entries, const u32 entryCount, const String tooltip) {
  ui_style_push(c);
  ui_style_outline(c, 0);

  f32 t = 0;
  for (u32 i = 0; i != entryCount; ++i) {
    const f32 frac = math_min(entries[i].frac, 1.0f - t);
    if (frac < f32_epsilon) {
      continue;
    }
    ui_layout_push(c);
    ui_layout_move(c, ui_vector(t, 0), UiBase_Current, Ui_X);
    ui_layout_resize(c, UiAlign_BottomLeft, ui_vector(frac, 0), UiBase_Current, Ui_X);
    ui_style_color(c, entries[i].color);
    ui_canvas_draw_glyph(c, UiShape_Square, 5, UiFlags_None);
    ui_layout_pop(c);
    t += frac;
  }

  ui_canvas_id_block_next(c); // Compensate for the potentially fluctuating amount of entries.

  if (!string_is_empty(tooltip)) {
    const UiId id = ui_canvas_id_peek(c);
    ui_canvas_draw_glyph(c, UiShape_Empty, 0, UiFlags_Interactable); // Invisible rect.
    ui_tooltip(c, id, tooltip, .variation = UiVariation_Monospace);
  }
  ui_style_pop(c);
}

static void
stats_draw_cpu_chart(UiCanvasComp* c, const DebugStatsComp* st, const RendStatsComp* rendSt) {
  stats_draw_bg(c, DebugBgFlags_None);
  stats_draw_label(c, string_lit("CPU"));

  ui_layout_push(c);
  ui_style_push(c);

  ui_layout_grow(c, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

  /**
   * We determine the cpu 'busy' time by subtracting the time we've spend blocking on the renderer.
   */
  f32 busyFrac = 1.0f;
  busyFrac -= st->rendWaitForGpuFrac;
  busyFrac -= st->rendPresAcqFrac;
  busyFrac -= st->rendPresEnqFrac;
  busyFrac -= st->rendPresWaitFrac;
  busyFrac -= st->rendLimiterFrac;

  const StatChartEntry entries[] = {
      {math_max(busyFrac, 0), ui_color(0, 128, 0, 210)},
      {st->rendWaitForGpuFrac, ui_color(255, 0, 0, 64)},
      {st->rendPresAcqFrac, ui_color(128, 0, 128, 64)},
      {st->rendPresEnqFrac, ui_color(0, 0, 255, 64)},
      {st->rendPresWaitFrac, ui_color(0, 128, 128, 64)},
      {st->rendLimiterFrac, ui_color(128, 128, 128, 64)},
  };
  const String tooltip = fmt_write_scratch(
      "\a~red\a.bWait for gpu\ar:\a>10{>8}\n"
      "\a~purple\a.bPresent acquire\ar:\a>10{>8}\n"
      "\a~blue\a.bPresent enqueue\ar:\a>10{>8}\n"
      "\a~teal\a.bPresent wait\ar:\a>10{>8}\n"
      "\a.bLimiter\ar:\a>10{>8}",
      fmt_duration(rendSt->waitForGpuDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->presentAcquireDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->presentEnqueueDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->presentWaitDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->limiterDur, .minDecDigits = 1, .maxDecDigits = 1));

  stats_draw_chart(c, entries, array_elems(entries), tooltip);

  ui_style_pop(c);
  ui_layout_pop(c);
  ui_layout_next(c, Ui_Down, 0);
}

static void
stats_draw_gpu_chart(UiCanvasComp* c, const DebugStatsComp* st, const RendStatsComp* rendSt) {
  stats_draw_bg(c, DebugBgFlags_None);
  stats_draw_label(c, string_lit("GPU"));

  ui_layout_push(c);
  ui_style_push(c);

  ui_layout_grow(c, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

  StatChartEntry entries[rend_stats_max_passes + 2 /* +2 for the 'other' and 'wait' entries */];

  Mem       tooltipBuffer = alloc_alloc(g_allocScratch, 4 * usize_kibibyte, 1);
  DynString tooltip       = dynstring_create_over(tooltipBuffer);

  f32 otherFrac = st->gpuExecFrac;
  for (u32 passIdx = 0; passIdx != rendSt->passCount; ++passIdx) {
    const String       passName     = rendSt->passes[passIdx].name;
    const TimeDuration passDuration = rendSt->passes[passIdx].gpuExecDur;
    const UiColor      passColor    = g_statsChartColors[passIdx % array_elems(g_statsChartColors)];
    const f32          passFrac     = st->gpuPassFrac[passIdx];

    entries[passIdx] = (StatChartEntry){
        .frac  = passFrac,
        .color = ui_color(passColor.r, passColor.g, passColor.b, 178),
    };
    otherFrac -= entries[passIdx].frac;

    fmt_write(
        &tooltip,
        "{}\a.b{}\ar:\a>0A{>7}\n",
        fmt_ui_color(passColor),
        fmt_text(passName),
        fmt_duration(passDuration, .minDecDigits = 1, .maxDecDigits = 1));
  }
  entries[rendSt->passCount + 0] = (StatChartEntry){
      .frac  = otherFrac,
      .color = ui_color(128, 128, 128, 178),
  };
  entries[rendSt->passCount + 1] = (StatChartEntry){
      .frac  = st->gpuWaitFrac,
      .color = ui_color(0, 128, 128, 64),
  };
  fmt_write(
      &tooltip,
      "\a.bTotal\ar:\a>0A{>7}\n"
      "\a~teal\a.bWait\ar:\a>0A{>7}",
      fmt_duration(rendSt->gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->gpuWaitDur, .minDecDigits = 1, .maxDecDigits = 1));

  stats_draw_chart(c, entries, rendSt->passCount + 2, dynstring_view(&tooltip));

  ui_style_pop(c);
  ui_layout_pop(c);
  ui_layout_next(c, Ui_Down, 0);
}

static void stats_draw_renderer_pass_dropdown(
    UiCanvasComp* c, DebugStatsComp* stats, const RendStatsComp* rendStats) {
  stats_draw_bg(c, DebugBgFlags_None);
  stats_draw_label(c, string_lit("Pass select"));
  {
    ui_layout_push(c);
    ui_style_push(c);

    ui_layout_grow(c, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

    String passNames[rend_stats_max_passes];
    for (u32 i = 0; i != rendStats->passCount; ++i) {
      passNames[i] = rendStats->passes[i].name;
    }
    stats->inspectPassIndex = math_min(stats->inspectPassIndex, rendStats->passCount - 1);

    ui_select(
        c,
        (i32*)&stats->inspectPassIndex,
        passNames,
        rendStats->passCount,
        .frameColor     = ui_color(24, 24, 24, 128),
        .dropFrameColor = ui_color(24, 24, 24, 225));

    ui_style_pop(c);
    ui_layout_pop(c);
  }
  ui_layout_next(c, Ui_Down, 0);
}

static void stats_draw_nav_layer_dropdown(UiCanvasComp* c, const DebugStatsComp* stats) {
  stats_draw_bg(c, DebugBgFlags_None);
  stats_draw_label(c, string_lit("Layer"));
  {
    ui_layout_push(c);
    ui_style_push(c);

    ui_layout_grow(c, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

    ui_select(
        c,
        (i32*)&stats->inspectNavLayer,
        g_sceneNavLayerNames,
        SceneNavLayer_Count,
        .frameColor     = ui_color(24, 24, 24, 128),
        .dropFrameColor = ui_color(24, 24, 24, 225));

    ui_style_pop(c);
    ui_layout_pop(c);
  }
  ui_layout_next(c, Ui_Down, 0);
}

static void stats_draw_notifications(UiCanvasComp* c, const DebugStatsGlobalComp* statsGlobal) {
  dynarray_for_t(&statsGlobal->notifications, DebugStatsNotification, notif) {
    const String key   = mem_create(notif->key, notif->keyLength);
    const String value = mem_create(notif->value, notif->valueLength);
    stats_draw_val_entry(c, key, value);
  }
}

static void debug_stats_draw_interface(
    UiCanvasComp*                  c,
    const GapWindowComp*           window,
    const DebugStatsGlobalComp*    statsGlobal,
    DebugStatsComp*                stats,
    const RendStatsComp*           rendStats,
    const AllocStats*              allocStats,
    const EcsDef*                  ecsDef,
    const EcsWorldStats*           ecsWorldStats,
    const EcsRunnerStats*          ecsRunnerStats,
    const SceneCollisionStatsComp* colStats,
    const VfxStatsGlobalComp*      vfxStats,
    const SceneNavEnvComp*         navEnv,
    const UiStatsComp*             uiStats) {

  ui_layout_move_to(c, UiBase_Container, UiAlign_TopLeft, Ui_XY);
  ui_layout_resize(c, UiAlign_TopLeft, ui_vector(500, 25), UiBase_Absolute, Ui_XY);

  // clang-format off
  stats_draw_frametime(c, stats);
  stats_draw_plot_dur(c, stats->frameDurPlot, 0, stats->frameDurDesired * 2);
  stats_draw_cpu_chart(c, stats, rendStats);
  stats_draw_gpu_chart(c, stats, rendStats);
  stats_draw_notifications(c, statsGlobal);

  if(stats->show != DebugStatShow_Full) {
    return;
  }

  if(stats_draw_section(c, string_lit("Window"))) {
    const GapVector windowSize = gap_window_param(window, GapParam_WindowSize);
    stats_draw_val_entry(c, string_lit("Size"), fmt_write_scratch("{}", gap_vector_fmt(windowSize)));
    stats_draw_val_entry(c, string_lit("Display"), gap_window_display_name(window));
    stats_draw_val_entry(c, string_lit("Refresh rate"), fmt_write_scratch("{}hz", fmt_float(gap_window_refresh_rate(window))));
    stats_draw_val_entry(c, string_lit("Dpi"), fmt_write_scratch("{}", fmt_int(gap_window_dpi(window))));
  }
  if(stats_draw_section(c, string_lit("Renderer"))) {
    const TimeDuration gpuExecDurAvg = debug_plot_avg_dur(stats->gpuExecDurPlot);

    stats_draw_val_entry(c, string_lit("Gpu"), fmt_write_scratch("{}", fmt_text(rendStats->gpuName)));
    stats_draw_val_entry(c, string_lit("Gpu exec duration"), fmt_write_scratch("{<9} frac: {}", fmt_duration(gpuExecDurAvg), fmt_float(stats->gpuExecFrac, .minDecDigits = 2, .maxDecDigits = 2)));
    stats_draw_plot_dur(c, stats->gpuExecDurPlot, 0, stats->frameDurDesired * 2);
    stats_draw_val_entry(c, string_lit("Swapchain"), fmt_write_scratch("images: {} present: {}", fmt_int(rendStats->swapchainImageCount), fmt_int(rendStats->swapchainPresentId)));
    stats_draw_val_entry(c, string_lit("Attachments"), fmt_write_scratch("{<3} ({})", fmt_int(rendStats->attachCount), fmt_size(rendStats->attachMemory)));
    stats_draw_val_entry(c, string_lit("Samplers"), fmt_write_scratch("{}", fmt_int(rendStats->samplerCount)));
    stats_draw_val_entry(c, string_lit("Descriptor sets"), fmt_write_scratch("{<3} reserved: {}", fmt_int(rendStats->descSetsOccupied), fmt_int(rendStats->descSetsReserved)));
    stats_draw_val_entry(c, string_lit("Descriptor layouts"), fmt_write_scratch("{}", fmt_int(rendStats->descLayouts)));
    stats_draw_val_entry(c, string_lit("Graphic resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatsRes_Graphic])));
    stats_draw_val_entry(c, string_lit("Shader resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatsRes_Shader])));
    stats_draw_val_entry(c, string_lit("Mesh resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatsRes_Mesh])));
    stats_draw_val_entry(c, string_lit("Texture resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatsRes_Texture])));

    stats_draw_renderer_pass_dropdown(c, stats, rendStats);
    const TimeDuration   frameDurAvg = debug_plot_avg_dur(stats->frameDurPlot);
    const RendStatsPass* passStats   = &rendStats->passes[stats->inspectPassIndex];
    const f32            passDurFrac = debug_frame_frac(frameDurAvg, passStats->gpuExecDur);
    stats_draw_val_entry(c, string_lit("Pass resolution max"), fmt_write_scratch("{}x{}", fmt_int(passStats->sizeMax[0]), fmt_int(passStats->sizeMax[1])));
    stats_draw_val_entry(c, string_lit("Pass exec duration"), fmt_write_scratch("{<10} frac: {}", fmt_duration(passStats->gpuExecDur), fmt_float(passDurFrac, .minDecDigits = 2, .maxDecDigits = 2)));
    stats_draw_val_entry(c, string_lit("Pass invocations"), fmt_write_scratch("{}", fmt_int(passStats->invocations)));
    stats_draw_val_entry(c, string_lit("Pass draws"), fmt_write_scratch("{}", fmt_int(passStats->draws)));
    stats_draw_val_entry(c, string_lit("Pass instances"), fmt_write_scratch("{}", fmt_int(passStats->instances)));
    stats_draw_val_entry(c, string_lit("Pass vertices"), fmt_write_scratch("{}", fmt_int(passStats->vertices)));
    stats_draw_val_entry(c, string_lit("Pass primitives"), fmt_write_scratch("{}", fmt_int(passStats->primitives)));
    stats_draw_val_entry(c, string_lit("Pass vertex-shaders"), fmt_write_scratch("{}", fmt_int(passStats->shadersVert)));
    stats_draw_val_entry(c, string_lit("Pass fragment-shaders"), fmt_write_scratch("{}", fmt_int(passStats->shadersFrag)));
  }
  if(stats_draw_section(c, string_lit("Memory"))) {
    const i64       pageDelta         = allocStats->pageCounter - statsGlobal->allocPrevPageCounter;
    const FormatArg pageDeltaColor    = pageDelta > 0 ? fmt_ui_color(ui_color_red) : fmt_nop();
    const i64       heapDelta         = allocStats->heapCounter - statsGlobal->allocPrevHeapCounter;
    const FormatArg heapDeltaColor    = heapDelta > 0 ? fmt_ui_color(ui_color_yellow) : fmt_nop();
    const i64       persistDelta      = allocStats->persistCounter - statsGlobal->allocPrevPersistCounter;
    const FormatArg persistDeltaColor = persistDelta > 0 ? fmt_ui_color(ui_color_red) : fmt_nop();

    stats_draw_val_entry(c, string_lit("Main"), fmt_write_scratch("{<11} pages: {}", fmt_size(allocStats->pageTotal), fmt_int(allocStats->pageCount)));
    stats_draw_val_entry(c, string_lit("Page counter"), fmt_write_scratch("count:  {<7} {}delta: {}\ar", fmt_int(allocStats->pageCounter), pageDeltaColor, fmt_int(pageDelta)));
    stats_draw_val_entry(c, string_lit("Heap"), fmt_write_scratch("active: {}", fmt_int(allocStats->heapActive)));
    stats_draw_val_entry(c, string_lit("Heap counter"), fmt_write_scratch("count:  {<7} {}delta: {}\ar", fmt_int(allocStats->heapCounter), heapDeltaColor, fmt_int(heapDelta)));
    if(stats_draw_button_entry(c, string_lit("Heap tracking"), string_lit("Dump"))) {
      alloc_heap_dump();
    }
    stats_draw_val_entry(c, string_lit("Persist counter"), fmt_write_scratch("count:  {<7} {}delta: {}\ar", fmt_int(allocStats->persistCounter), persistDeltaColor, fmt_int(persistDelta)));
    if(stats_draw_button_entry(c, string_lit("Persist tracking"), string_lit("Dump"))) {
      alloc_persist_dump();
    }
    stats_draw_val_entry(c, string_lit("Renderer chunks"), fmt_write_scratch("{}", fmt_int(rendStats->memChunks)));
    stats_draw_val_entry(c, string_lit("Renderer"), fmt_write_scratch("{<8} reserved: {}", fmt_size(rendStats->ramOccupied), fmt_size(rendStats->ramReserved)));
    stats_draw_val_entry(c, string_lit("GPU (on device)"), fmt_write_scratch("{<8} reserved: {}", fmt_size(rendStats->vramOccupied), fmt_size(rendStats->vramReserved)));
    stats_draw_val_entry(c, string_lit("File"), fmt_write_scratch("handles: {<3} map: {}", fmt_int(statsGlobal->fileCount), fmt_size(statsGlobal->fileMappingSize)));
    stats_draw_val_entry(c, string_lit("DynLib"), fmt_write_scratch("handles: {<3}", fmt_int(statsGlobal->dynlibCount)));
    stats_draw_val_entry(c, string_lit("StringTable"), fmt_write_scratch("global: {}", fmt_int(statsGlobal->globalStringCount)));
    stats_draw_val_entry(c, string_lit("Data"), fmt_write_scratch("types: {}", fmt_int(data_type_count(g_dataReg))));
  }
  if(stats_draw_section(c, string_lit("ECS"))) {
    const TimeDuration flushDurAvg = debug_plot_avg_dur(statsGlobal->ecsFlushDurPlot);
    const TimeDuration flushDurMax = debug_plot_max_dur(statsGlobal->ecsFlushDurPlot);

    stats_draw_val_entry(c, string_lit("Components"), fmt_write_scratch("{}", fmt_int(ecs_def_comp_count(ecsDef))));
    stats_draw_val_entry(c, string_lit("Views"), fmt_write_scratch("{}", fmt_int(ecs_def_view_count(ecsDef))));
    stats_draw_val_entry(c, string_lit("Systems"), fmt_write_scratch("{}", fmt_int(ecs_def_system_count(ecsDef))));
    stats_draw_val_entry(c, string_lit("Modules"), fmt_write_scratch("{}", fmt_int(ecs_def_module_count(ecsDef))));
    stats_draw_val_entry(c, string_lit("Entities"), fmt_write_scratch("{}", fmt_int(ecsWorldStats->entityCount)));
    stats_draw_val_entry(c, string_lit("Archetypes"), fmt_write_scratch("{<8} empty:  {}", fmt_int(ecsWorldStats->archetypeCount), fmt_int(ecsWorldStats->archetypeEmptyCount)));
    stats_draw_val_entry(c, string_lit("Archetype data"), fmt_write_scratch("{<8} chunks: {}", fmt_size(ecsWorldStats->archetypeTotalSize), fmt_int(ecsWorldStats->archetypeTotalChunks)));
    stats_draw_val_entry(c, string_lit("Plan"), fmt_write_scratch("{<8} est:    {}", fmt_int(ecsRunnerStats->planCounter), fmt_duration(ecsRunnerStats->planEstSpan)));
    stats_draw_val_entry(c, string_lit("Flush duration"), fmt_write_scratch("{<8} max:    {}", fmt_duration(flushDurAvg), fmt_duration(flushDurMax)));
    stats_draw_val_entry(c, string_lit("Flush entities"), fmt_write_scratch("{}", fmt_int(ecsWorldStats->lastFlushEntities)));
  }
  if(stats_draw_section(c, string_lit("Collision"))) {
    stats_draw_val_entry(c, string_lit("Prim spheres"), fmt_write_scratch("{}", fmt_int(colStats->queryStats[GeoQueryStat_PrimSphereCount])));
    stats_draw_val_entry(c, string_lit("Prim capsules"), fmt_write_scratch("{}", fmt_int(colStats->queryStats[GeoQueryStat_PrimCapsuleCount])));
    stats_draw_val_entry(c, string_lit("Prim box-rotated"), fmt_write_scratch("{}", fmt_int(colStats->queryStats[GeoQueryStat_PrimBoxRotatedCount])));
    stats_draw_val_entry(c, string_lit("Bvh"), fmt_write_scratch("nodes:  {<5} depth: {}", fmt_int(colStats->queryStats[GeoQueryStat_BvhNodes]), fmt_int(colStats->queryStats[GeoQueryStat_BvhMaxDepth])));
    stats_draw_val_entry(c, string_lit("Query ray"), fmt_write_scratch("normal: {<5} fat: {}", fmt_int(colStats->queryStats[GeoQueryStat_QueryRayCount]), fmt_int(colStats->queryStats[GeoQueryStat_QueryRayFatCount])));
    stats_draw_val_entry(c, string_lit("Query all"), fmt_write_scratch("sphere: {<5} box: {}", fmt_int(colStats->queryStats[GeoQueryStat_QuerySphereAllCount]), fmt_int(colStats->queryStats[GeoQueryStat_QueryBoxAllCount])));
  }
  if(stats_draw_section(c, string_lit("VFX"))) {
    for (VfxStat vfxStat = 0; vfxStat != VfxStat_Count; ++vfxStat) {
      const i32 val = vfx_stats_get(&vfxStats->set, vfxStat);
      stats_draw_val_entry(c, vfx_stats_name(vfxStat), fmt_write_scratch("{}", fmt_int(val)));
    }
  }
  if(stats_draw_section(c, string_lit("Navigation"))) {
    stats_draw_nav_layer_dropdown(c, stats);
    const u32* navStats = scene_nav_grid_stats(navEnv, stats->inspectNavLayer);
    stats_draw_val_entry(c, string_lit("Cells"), fmt_write_scratch("total: {<6} axis: {}", fmt_int(navStats[GeoNavStat_CellCountTotal]), fmt_int(navStats[GeoNavStat_CellCountAxis])));
    stats_draw_val_entry(c, string_lit("Grid data"), fmt_write_scratch("{}", fmt_size(navStats[GeoNavStat_GridDataSize])));
    stats_draw_val_entry(c, string_lit("Worker data"), fmt_write_scratch("{}", fmt_size(navStats[GeoNavStat_WorkerDataSize])));
    stats_draw_val_entry(c, string_lit("Blockers"), fmt_write_scratch("total: {<4} additions: {}", fmt_int(navStats[GeoNavStat_BlockerCount]), fmt_int(navStats[GeoNavStat_BlockerAddCount])));
    stats_draw_val_entry(c, string_lit("Occupants"), fmt_write_scratch("{}", fmt_int(navStats[GeoNavStat_OccupantCount])));
    stats_draw_val_entry(c, string_lit("Islands"), fmt_write_scratch("{<11} computes: {}", fmt_int(navStats[GeoNavStat_IslandCount]), fmt_int(navStats[GeoNavStat_IslandComputes])));
    stats_draw_val_entry(c, string_lit("Path count"), fmt_write_scratch("{<11} limiter: {}", fmt_int(navStats[GeoNavStat_PathCount]), fmt_int(navStats[GeoNavStat_PathLimiterCount])));
    stats_draw_val_entry(c, string_lit("Path output"), fmt_write_scratch("cells: {}", fmt_int(navStats[GeoNavStat_PathOutputCells])));
    stats_draw_val_entry(c, string_lit("Path iterations"), fmt_write_scratch("cells: {<4} enqueues: {}", fmt_int(navStats[GeoNavStat_PathItrCells]), fmt_int(navStats[GeoNavStat_PathItrEnqueues])));
    stats_draw_val_entry(c, string_lit("Find count"), fmt_write_scratch("{}", fmt_int(navStats[GeoNavStat_FindCount])));
    stats_draw_val_entry(c, string_lit("Find iterations"), fmt_write_scratch("cells: {<4} enqueues: {}", fmt_int(navStats[GeoNavStat_FindItrCells]), fmt_int(navStats[GeoNavStat_FindItrEnqueues])));
    stats_draw_val_entry(c, string_lit("Channel queries"), fmt_write_scratch("{}", fmt_int(navStats[GeoNavStat_ChannelQueries])));
    stats_draw_val_entry(c, string_lit("Blocker reachable"), fmt_write_scratch("queries: {}", fmt_int(navStats[GeoNavStat_BlockerReachableQueries])));
    stats_draw_val_entry(c, string_lit("Blocker closest"), fmt_write_scratch("queries: {}", fmt_int(navStats[GeoNavStat_BlockerClosestQueries])));
  }
  if(stats_draw_section(c, string_lit("Interface"))) {
    stats_draw_val_entry(c, string_lit("Canvas size"), fmt_write_scratch("{}x{}", fmt_float(uiStats->canvasSize.x, .maxDecDigits = 0), fmt_float(uiStats->canvasSize.y, .maxDecDigits = 0)));
    stats_draw_val_entry(c, string_lit("Canvasses"), fmt_write_scratch("{}", fmt_int(uiStats->canvasCount)));
    stats_draw_val_entry(c, string_lit("Tracked elements"), fmt_write_scratch("{}", fmt_int(uiStats->trackedElemCount)));
    stats_draw_val_entry(c, string_lit("Persistent elements"), fmt_write_scratch("{}", fmt_int(uiStats->persistElemCount)));
    stats_draw_val_entry(c, string_lit("Atoms"), fmt_write_scratch("{<8} overlay: {}", fmt_int(uiStats->atomCount), fmt_int(uiStats->atomOverlayCount)));
    stats_draw_val_entry(c, string_lit("Clip-rects"), fmt_write_scratch("{}", fmt_int(uiStats->clipRectCount)));
    stats_draw_val_entry(c, string_lit("Commands"), fmt_write_scratch("{}", fmt_int(uiStats->commandCount)));
  }
  // clang-format on
}

static void debug_stats_update(
    DebugStatsComp*               stats,
    const GapWindowComp*          window,
    const RendStatsComp*          rendStats,
    const RendSettingsGlobalComp* rendGlobalSettings,
    const SceneTimeComp*          time) {

  const TimeDuration frameDur = time->realDelta;
  debug_plot_add_dur(stats->frameDurPlot, frameDur);

  if (rendGlobalSettings->limiterFreq) {
    stats->frameDurDesired = time_second / rendGlobalSettings->limiterFreq;
  } else {
    stats->frameDurDesired = (TimeDuration)((f64)time_second / gap_window_refresh_rate(window));
  }

  debug_plot_add_dur(stats->gpuExecDurPlot, rendStats->gpuExecDur);

  debug_avg_f32(&stats->rendWaitForGpuFrac, debug_frame_frac(frameDur, rendStats->waitForGpuDur));
  debug_avg_f32(&stats->rendPresAcqFrac, debug_frame_frac(frameDur, rendStats->presentAcquireDur));
  debug_avg_f32(&stats->rendPresEnqFrac, debug_frame_frac(frameDur, rendStats->presentEnqueueDur));
  debug_avg_f32(&stats->rendPresWaitFrac, debug_frame_frac(frameDur, rendStats->presentWaitDur));
  debug_avg_f32(&stats->rendLimiterFrac, debug_frame_frac(frameDur, rendStats->limiterDur));
  debug_avg_f32(&stats->gpuWaitFrac, debug_frame_frac(frameDur, rendStats->gpuWaitDur));
  debug_avg_f32(&stats->gpuExecFrac, debug_frame_frac(frameDur, rendStats->gpuExecDur));
  for (u32 pass = 0; pass != rendStats->passCount; ++pass) {
    const f32 passFrac = debug_frame_frac(frameDur, rendStats->passes[pass].gpuExecDur);
    debug_avg_f32(&stats->gpuPassFrac[pass], passFrac);
  }
}

static void
debug_stats_global_update(DebugStatsGlobalComp* statsGlobal, const EcsRunnerStats* ecsRunnerStats) {

  const TimeReal oldestNotifToKeep = time_real_offset(time_real_clock(), -stats_notify_max_age);
  debug_notify_prune_older(statsGlobal, oldestNotifToKeep);

  statsGlobal->fileCount         = file_count();
  statsGlobal->fileMappingSize   = file_mapping_size();
  statsGlobal->dynlibCount       = dynlib_count();
  statsGlobal->globalStringCount = stringtable_count(g_stringtable);

  debug_plot_add(
      statsGlobal->ecsFlushDurPlot, (f32)(ecsRunnerStats->flushDurLast / (f64)time_microsecond));
}

ecs_view_define(GlobalView) {
  ecs_access_read(RendSettingsGlobalComp);
  ecs_access_read(SceneCollisionStatsComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_read(VfxStatsGlobalComp);
  ecs_access_write(DebugStatsGlobalComp);
}

ecs_view_define(StatsCreateView) {
  ecs_access_with(GapWindowComp);
  ecs_access_with(SceneCameraComp); // Only track stats for windows with 3d content.
  ecs_access_without(DebugStatsComp);
}

ecs_view_define(StatsUpdateView) {
  ecs_access_read(GapWindowComp);
  ecs_access_read(RendStatsComp);
  ecs_access_read(UiStatsComp);
  ecs_access_write(DebugStatsComp);
}

ecs_view_define(CanvasWriteView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the canvas's we create.
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugStatsCreateSys) {
  // Create a single global stats component.
  if (!ecs_world_has_t(world, ecs_world_global(world), DebugStatsGlobalComp)) {
    ecs_world_add_t(
        world,
        ecs_world_global(world),
        DebugStatsGlobalComp,
        .notifications   = dynarray_create_t(g_allocHeap, DebugStatsNotification, 8),
        .ecsFlushDurPlot = debug_plot_alloc(g_allocHeap));
  }

  // Create a stats component for each window with 3d content (so with a camera).
  EcsView* createView = ecs_world_view_t(world, StatsCreateView);
  for (EcsIterator* itr = ecs_view_itr(createView); ecs_view_walk(itr);) {
    ecs_world_add_t(
        world,
        ecs_view_entity(itr),
        DebugStatsComp,
        .frameDurPlot   = debug_plot_alloc(g_allocHeap),
        .gpuExecDurPlot = debug_plot_alloc(g_allocHeap));
  }
}

ecs_system_define(DebugStatsUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DebugStatsGlobalComp*          statsGlobal = ecs_view_write_t(globalItr, DebugStatsGlobalComp);
  const SceneTimeComp*           time        = ecs_view_read_t(globalItr, SceneTimeComp);
  const SceneCollisionStatsComp* colStats    = ecs_view_read_t(globalItr, SceneCollisionStatsComp);
  const VfxStatsGlobalComp*      vfxStats    = ecs_view_read_t(globalItr, VfxStatsGlobalComp);
  const SceneNavEnvComp*         navEnv      = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const RendSettingsGlobalComp*  rendGlobalSet = ecs_view_read_t(globalItr, RendSettingsGlobalComp);

  const AllocStats     allocStats     = alloc_stats_query();
  const EcsWorldStats  ecsWorldStats  = ecs_world_stats_query(world);
  const EcsRunnerStats ecsRunnerStats = ecs_runner_stats_query(g_ecsRunningRunner);
  debug_stats_global_update(statsGlobal, &ecsRunnerStats);

  EcsIterator* canvasItr = ecs_view_itr(ecs_world_view_t(world, CanvasWriteView));

  EcsView* statsView = ecs_world_view_t(world, StatsUpdateView);
  for (EcsIterator* itr = ecs_view_itr(statsView); ecs_view_walk(itr);) {
    DebugStatsComp*      stats     = ecs_view_write_t(itr, DebugStatsComp);
    const GapWindowComp* window    = ecs_view_read_t(itr, GapWindowComp);
    const RendStatsComp* rendStats = ecs_view_read_t(itr, RendStatsComp);
    const UiStatsComp*   uiStats   = ecs_view_read_t(itr, UiStatsComp);
    const EcsDef*        ecsDef    = ecs_world_def(world);

    // Update statistics.
    debug_stats_update(stats, window, rendStats, rendGlobalSet, time);

    // Create or destroy the interface canvas as needed.
    if (stats->show != DebugStatShow_None && !stats->canvas) {
      stats->canvas = ui_canvas_create(world, ecs_view_entity(itr), UiCanvasCreateFlags_ToBack);
    } else if (stats->show == DebugStatShow_None && stats->canvas) {
      ecs_world_entity_destroy(world, stats->canvas);
      stats->canvas = 0;
    }

    // Draw the interface.
    if (stats->canvas && ecs_view_maybe_jump(canvasItr, stats->canvas)) {
      UiCanvasComp* c = ecs_view_write_t(canvasItr, UiCanvasComp);
      ui_canvas_reset(c);
      debug_stats_draw_interface(
          c,
          window,
          statsGlobal,
          stats,
          rendStats,
          &allocStats,
          ecsDef,
          &ecsWorldStats,
          &ecsRunnerStats,
          colStats,
          vfxStats,
          navEnv,
          uiStats);
    }
  }

  statsGlobal->allocPrevPageCounter    = allocStats.pageCounter;
  statsGlobal->allocPrevHeapCounter    = allocStats.heapCounter;
  statsGlobal->allocPrevPersistCounter = allocStats.persistCounter;
}

ecs_module_init(debug_stats_module) {
  ecs_register_comp(DebugStatsComp, .destructor = ecs_destruct_stats);
  ecs_register_comp(DebugStatsGlobalComp, .destructor = ecs_destruct_stats_global);

  ecs_register_view(GlobalView);
  ecs_register_view(StatsCreateView);
  ecs_register_view(StatsUpdateView);
  ecs_register_view(CanvasWriteView);

  ecs_register_system(DebugStatsCreateSys, ecs_view_id(StatsCreateView));
  ecs_register_system(
      DebugStatsUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(StatsUpdateView),
      ecs_view_id(CanvasWriteView));
}

void debug_stats_notify(DebugStatsGlobalComp* comp, const String key, const String value) {
  DebugStatsNotification* notif = debug_notify_get(comp, key);
  notif->timestamp              = time_real_clock();
  notif->valueLength            = math_min((u8)value.size, stats_notify_max_value_size);
  mem_cpy(mem_create(notif->value, notif->valueLength), string_slice(value, 0, notif->valueLength));
}

DebugStatShow debug_stats_show(const DebugStatsComp* comp) { return comp->show; }

void debug_stats_show_set(DebugStatsComp* comp, const DebugStatShow show) { comp->show = show; }
