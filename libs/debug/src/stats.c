#include "core_alloc.h"
#include "core_array.h"
#include "core_dynlib.h"
#include "core_file.h"
#include "core_float.h"
#include "core_format.h"
#include "core_math.h"
#include "core_stringtable.h"
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

static const f32 g_statsLabelWidth       = 210;
static const u8  g_statsBgAlpha          = 150;
static const u8  g_statsSectionBgAlpha   = 200;
static const f32 g_statsInvAverageWindow = 1.0f / 10.0f;

#define stats_plot_size 32
#define stats_notify_max_key_size 32
#define stats_notify_max_value_size 16
#define stats_notify_max_age time_seconds(3)

typedef enum {
  DebugBgFlags_None    = 0,
  DebugBgFlags_Section = 1 << 0,
} DebugBgFlags;

typedef struct {
  f32 values[stats_plot_size];
  u32 cur;
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

  TimeDuration  frameDur;
  TimeDuration  frameDurAvg;
  DebugStatPlot frameDurVarianceUs; // In microseconds.
  f32           frameFreqAvg;
  TimeDuration  frameDurDesired;

  RendPass      passInspect;     // Pass to show stats for.
  SceneNavLayer navLayerInspect; // Navigation layer to show stats for.

  // Cpu frame fractions.
  f32 rendWaitForGpuFrac, rendPresAcqFrac, rendPresEnqFrac, rendPresWaitFrac, rendLimiterFrac;

  // Gpu frame fractions.
  f32 gpuExecFrac;
  f32 gpuPassFrac[RendPass_Count];
};

ecs_comp_define(DebugStatsGlobalComp) {
  DynArray notifications; // DebugStatsNotification[].

  u64 allocPrevPageCounter, allocPrevHeapCounter, allocPrevPersistCounter;
  u32 fileCount, dynlibCount;
  u32 globalStringCount;

  DebugStatPlot ecsFlushDurUs; // In microseconds.
};

static void ecs_destruct_stats_global(void* data) {
  DebugStatsGlobalComp* comp = data;
  dynarray_destroy(&comp->notifications);
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

static void debug_plot_add(DebugStatPlot* plot, const f32 value) {
  plot->values[plot->cur] = value;
  plot->cur               = (plot->cur + 1) % stats_plot_size;
}

static f32 debug_plot_max(const DebugStatPlot* plot) {
  f32 max = plot->values[0];
  for (u32 i = 1; i != stats_plot_size; ++i) {
    if (plot->values[i] > max) {
      max = plot->values[i];
    }
  }
  return max;
}

static void debug_avg_f32(f32* value, const f32 new) {
  *value += (new - *value) * g_statsInvAverageWindow;
}

static void debug_avg_f64(f64* value, const f64 new) {
  *value += (new - *value) * g_statsInvAverageWindow;
}

static void debug_avg_dur(TimeDuration* value, const TimeDuration new) {
  f64 floatVal = (f64)*value;
  debug_avg_f64(&floatVal, (f64) new);
  *value = (TimeDuration)floatVal;
}

static f32 debug_frame_frac(const DebugStatsComp* stats, const TimeDuration dur) {
  return math_clamp_f32(dur / (f32)stats->frameDurAvg, 0, 1);
}

static void stats_draw_bg(UiCanvasComp* canvas, const DebugBgFlags flags) {
  ui_style_push(canvas);
  const u8 alpha = flags & DebugBgFlags_Section ? g_statsSectionBgAlpha : g_statsBgAlpha;
  ui_style_color(canvas, ui_color(0, 0, 0, alpha));
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
  ui_style_pop(canvas);
}

static void stats_draw_label(UiCanvasComp* canvas, const String label) {
  ui_layout_push(canvas);

  ui_layout_resize(
      canvas, UiAlign_BottomLeft, ui_vector(g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);
  ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);
  ui_label(canvas, label, .align = UiAlign_MiddleLeft);

  ui_layout_pop(canvas);
}

static void stats_draw_value(UiCanvasComp* canvas, const String value) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_layout_grow(
      canvas, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

  ui_style_variation(canvas, UiVariation_Monospace);
  ui_style_weight(canvas, UiWeight_Bold);
  ui_label(canvas, value, .selectable = true);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void stats_draw_val_entry(UiCanvasComp* canvas, const String label, const String value) {
  stats_draw_bg(canvas, DebugBgFlags_None);
  stats_draw_label(canvas, label);
  stats_draw_value(canvas, value);
  ui_layout_next(canvas, Ui_Down, 0);
}

static bool stats_draw_section(UiCanvasComp* canvas, const String label) {
  ui_canvas_id_block_next(canvas);
  stats_draw_bg(canvas, DebugBgFlags_Section);
  const bool isOpen = ui_section(canvas, .label = label);
  ui_layout_next(canvas, Ui_Down, 0);
  return isOpen;
}

static void stats_draw_frametime(UiCanvasComp* canvas, const DebugStatsComp* stats) {
  const f64 g_errorThreshold = 1.25;
  const f64 g_warnThreshold  = 1.05;

  String colorText = string_empty;
  if (stats->frameDur > stats->frameDurDesired * g_errorThreshold) {
    colorText = ui_escape_color_scratch(ui_color_red);
  } else if (stats->frameDur > stats->frameDurDesired * g_warnThreshold) {
    colorText = ui_escape_color_scratch(ui_color_yellow);
  }
  const f32    varianceUs = debug_plot_max(&stats->frameDurVarianceUs);
  const String freqText   = fmt_write_scratch(
      "{}hz", fmt_float(stats->frameFreqAvg, .minDecDigits = 1, .maxDecDigits = 1));

  stats_draw_val_entry(
      canvas,
      string_lit("Frame time"),
      fmt_write_scratch(
          "{}{<8}{<8}{>7} var",
          fmt_text(colorText),
          fmt_duration(stats->frameDurAvg, .minDecDigits = 1),
          fmt_text(freqText),
          fmt_duration((TimeDuration)(varianceUs * time_microsecond), .maxDecDigits = 0)));
}

typedef struct {
  f32     frac;
  UiColor color;
} StatChartEntry;

static void stats_draw_chart(
    UiCanvasComp*         canvas,
    const StatChartEntry* entries,
    const u32             entryCount,
    const String          tooltip) {
  ui_style_push(canvas);
  ui_style_outline(canvas, 0);

  f32 t = 0;
  for (u32 i = 0; i != entryCount; ++i) {
    const f32 frac = math_min(entries[i].frac, 1.0f - t);
    if (frac < f32_epsilon) {
      continue;
    }
    ui_layout_push(canvas);
    ui_layout_move(canvas, ui_vector(t, 0), UiBase_Current, Ui_X);
    ui_layout_resize(canvas, UiAlign_BottomLeft, ui_vector(frac, 0), UiBase_Current, Ui_X);
    ui_style_color(canvas, entries[i].color);
    ui_canvas_draw_glyph(canvas, UiShape_Square, 5, UiFlags_None);
    ui_layout_pop(canvas);
    t += frac;
  }

  ui_canvas_id_block_next(canvas); // Compensate for the potentially fluctuating amount of entries.

  if (!string_is_empty(tooltip)) {
    const UiId id = ui_canvas_id_peek(canvas);
    ui_canvas_draw_glyph(canvas, UiShape_Empty, 0, UiFlags_Interactable); // Invisible rect.
    ui_tooltip(canvas, id, tooltip, .variation = UiVariation_Monospace);
  }
  ui_style_pop(canvas);
}

static void
stats_draw_cpu_chart(UiCanvasComp* canvas, const DebugStatsComp* st, const RendStatsComp* rendSt) {
  stats_draw_bg(canvas, DebugBgFlags_None);
  stats_draw_label(canvas, string_lit("CPU"));

  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_layout_grow(
      canvas, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

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
      {math_max(busyFrac, 0), ui_color(0, 128, 0, 178)},
      {st->rendWaitForGpuFrac, ui_color(255, 0, 0, 178)},
      {st->rendPresAcqFrac, ui_color(128, 0, 128, 178)},
      {st->rendPresEnqFrac, ui_color(0, 0, 255, 178)},
      {st->rendPresWaitFrac, ui_color(0, 128, 128, 128)},
      {st->rendLimiterFrac, ui_color(128, 128, 128, 128)},
  };
  const String tooltip = fmt_write_scratch(
      "\a~red\a.bWait for gpu\ar:\a>12{>8}\n"
      "\a~purple\a.bPresent acquire\ar:\a>12{>8}\n"
      "\a~blue\a.bPresent enqueue\ar:\a>12{>8}\n"
      "\a~teal\a.bPresent wait\ar:\a>12{>8}\n"
      "\a.bLimiter\ar:\a>12{>8}",
      fmt_duration(rendSt->waitForGpuDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->presentAcquireDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->presentEnqueueDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->presentWaitDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->limiterDur, .minDecDigits = 1, .maxDecDigits = 1));

  stats_draw_chart(canvas, entries, array_elems(entries), tooltip);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
  ui_layout_next(canvas, Ui_Down, 0);
}

static void
stats_draw_gpu_chart(UiCanvasComp* canvas, const DebugStatsComp* st, const RendStatsComp* rendSt) {
  stats_draw_bg(canvas, DebugBgFlags_None);
  stats_draw_label(canvas, string_lit("GPU"));

  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_layout_grow(
      canvas, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

  f32 otherFrac = st->gpuExecFrac;
  for (RendPass pass = 0; pass != RendPass_Count; ++pass) {
    otherFrac -= st->gpuPassFrac[pass];
  }

  const StatChartEntry entries[] = {
      {st->gpuPassFrac[RendPass_Geometry], ui_color(0, 128, 128, 178)},
      {st->gpuPassFrac[RendPass_Decal], ui_color(0, 0, 128, 178)},
      {st->gpuPassFrac[RendPass_Fog], ui_color(128, 128, 0, 178)},
      {st->gpuPassFrac[RendPass_FogBlur], ui_color(128, 0, 0, 178)},
      {st->gpuPassFrac[RendPass_Shadow], ui_color(128, 0, 128, 178)},
      {st->gpuPassFrac[RendPass_AmbientOcclusion], ui_color(128, 128, 0, 178)},
      {st->gpuPassFrac[RendPass_Forward], ui_color(0, 128, 0, 178)},
      {st->gpuPassFrac[RendPass_Distortion], ui_color(255, 0, 255, 178)},
      {st->gpuPassFrac[RendPass_Bloom], ui_color(0, 0, 255, 178)},
      {st->gpuPassFrac[RendPass_Post], ui_color(128, 0, 0, 178)},
      {otherFrac, ui_color(128, 128, 128, 178)},
  };
  // clang-format off
  const String tooltip = fmt_write_scratch(
      "\a~teal\a.bGeometry\ar:\a>13{>7}\n"
      "\a~navy\a.bDecal\ar:\a>13{>7}\n"
      "\a~olive\a.bFog\ar:\a>13{>7}\n"
      "\a~maroon\a.bFogBlur\ar:\a>13{>7}\n"
      "\a~purple\a.bShadow\ar:\a>13{>7}\n"
      "\a~orange\a.bAmbientOcclusion\ar:\a>13{>7}\n"
      "\a~green\a.bForward\ar:\a>13{>7}\n"
      "\a~fuchsia\a.bDistortion\ar:\a>13{>7}\n"
      "\a~blue\a.bBloom\ar:\a>13{>7}\n"
      "\a~maroon\a.bPost\ar:\a>13{>7}\n"
      "\a.bTotal\ar:\a>13{>7}",
      fmt_duration(rendSt->passes[RendPass_Geometry].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->passes[RendPass_Decal].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->passes[RendPass_Fog].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->passes[RendPass_FogBlur].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->passes[RendPass_Shadow].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->passes[RendPass_AmbientOcclusion].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->passes[RendPass_Forward].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->passes[RendPass_Distortion].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->passes[RendPass_Bloom].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->passes[RendPass_Post].gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendSt->gpuExecDur, .minDecDigits = 1, .maxDecDigits = 1));
  // clang-format off

  stats_draw_chart(canvas, entries, array_elems(entries), tooltip);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
  ui_layout_next(canvas, Ui_Down, 0);
}

static void stats_draw_renderer_pass_dropdown(UiCanvasComp* canvas, const DebugStatsComp* stats) {
  stats_draw_bg(canvas, DebugBgFlags_None);
  stats_draw_label(canvas, string_lit("Pass select"));
  {
    ui_layout_push(canvas);
    ui_style_push(canvas);

    ui_layout_grow(
        canvas, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

    ui_select(
        canvas,
        (i32*)&stats->passInspect,
        g_rendPassNames,
        RendPass_Count,
        .frameColor     = ui_color(24, 24, 24, 128),
        .dropFrameColor = ui_color(24, 24, 24, 225));

    ui_style_pop(canvas);
    ui_layout_pop(canvas);
  }
  ui_layout_next(canvas, Ui_Down, 0);
}

static void stats_draw_nav_layer_dropdown(UiCanvasComp* canvas, const DebugStatsComp* stats) {
  stats_draw_bg(canvas, DebugBgFlags_None);
  stats_draw_label(canvas, string_lit("Layer"));
  {
    ui_layout_push(canvas);
    ui_style_push(canvas);

    ui_layout_grow(
        canvas, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);

    ui_select(
        canvas,
        (i32*)&stats->navLayerInspect,
        g_sceneNavLayerNames,
        SceneNavLayer_Count,
        .frameColor     = ui_color(24, 24, 24, 128),
        .dropFrameColor = ui_color(24, 24, 24, 225));

    ui_style_pop(canvas);
    ui_layout_pop(canvas);
  }
  ui_layout_next(canvas, Ui_Down, 0);
}

static void
stats_draw_notifications(UiCanvasComp* canvas, const DebugStatsGlobalComp* statsGlobal) {
  dynarray_for_t(&statsGlobal->notifications, DebugStatsNotification, notif) {
    const String key   = mem_create(notif->key, notif->keyLength);
    const String value = mem_create(notif->value, notif->valueLength);
    stats_draw_val_entry(canvas, key, value);
  }
}

static void debug_stats_draw_interface(
    UiCanvasComp*                  canvas,
    const DebugStatsGlobalComp*    statsGlobal,
    const DebugStatsComp*          stats,
    const RendStatsComp*           rendStats,
    const AllocStats*              allocStats,
    const EcsDef*                  ecsDef,
    const EcsWorldStats*           ecsWorldStats,
    const EcsRunnerStats*          ecsRunnerStats,
    const SceneCollisionStatsComp* colStats,
    const SceneNavEnvComp*         navEnv,
    const UiStatsComp*             uiStats) {

  ui_layout_move_to(canvas, UiBase_Container, UiAlign_TopLeft, Ui_XY);
  ui_layout_resize(canvas, UiAlign_TopLeft, ui_vector(500, 25), UiBase_Absolute, Ui_XY);

  // clang-format off
  stats_draw_frametime(canvas, stats);
  stats_draw_cpu_chart(canvas, stats, rendStats);
  stats_draw_gpu_chart(canvas, stats, rendStats);
  stats_draw_notifications(canvas, statsGlobal);

  if(stats->show != DebugStatShow_Full) {
    return;
  }

  if(stats_draw_section(canvas, string_lit("Renderer"))) {
    stats_draw_val_entry(canvas, string_lit("Gpu"), fmt_write_scratch("{}", fmt_text(rendStats->gpuName)));
    stats_draw_val_entry(canvas, string_lit("Gpu exec duration"), fmt_write_scratch("{<10} frac: {}", fmt_duration(rendStats->gpuExecDur), fmt_float(stats->gpuExecFrac, .minDecDigits = 2, .maxDecDigits = 2)));
    stats_draw_val_entry(canvas, string_lit("Attachments"), fmt_write_scratch("{<3} {}", fmt_int(rendStats->attachCount), fmt_size(rendStats->attachMemory)));
    stats_draw_val_entry(canvas, string_lit("Samplers"), fmt_write_scratch("{}", fmt_int(rendStats->samplerCount)));
    stats_draw_val_entry(canvas, string_lit("Descriptor sets"), fmt_write_scratch("{<3} reserved: {}", fmt_int(rendStats->descSetsOccupied), fmt_int(rendStats->descSetsReserved)));
    stats_draw_val_entry(canvas, string_lit("Descriptor layouts"), fmt_write_scratch("{}", fmt_int(rendStats->descLayouts)));
    stats_draw_val_entry(canvas, string_lit("Graphic resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Graphic])));
    stats_draw_val_entry(canvas, string_lit("Shader resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Shader])));
    stats_draw_val_entry(canvas, string_lit("Mesh resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Mesh])));
    stats_draw_val_entry(canvas, string_lit("Texture resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Texture])));

    stats_draw_renderer_pass_dropdown(canvas, stats);
    const f32 passDurFrac = debug_frame_frac(stats, rendStats->passes[stats->passInspect].gpuExecDur);
    stats_draw_val_entry(canvas, string_lit("Pass resolution max"), fmt_write_scratch("{}x{}", fmt_int(rendStats->passes[stats->passInspect].sizeMax[0]), fmt_int(rendStats->passes[stats->passInspect].sizeMax[1])));
    stats_draw_val_entry(canvas, string_lit("Pass exec duration"), fmt_write_scratch("{<10} frac: {}", fmt_duration(rendStats->passes[stats->passInspect].gpuExecDur), fmt_float(passDurFrac, .minDecDigits = 2, .maxDecDigits = 2)));
    stats_draw_val_entry(canvas, string_lit("Pass invocations"), fmt_write_scratch("{}", fmt_int(rendStats->passes[stats->passInspect].invocations)));
    stats_draw_val_entry(canvas, string_lit("Pass draws"), fmt_write_scratch("{}", fmt_int(rendStats->passes[stats->passInspect].draws)));
    stats_draw_val_entry(canvas, string_lit("Pass instances"), fmt_write_scratch("{}", fmt_int(rendStats->passes[stats->passInspect].instances)));
    stats_draw_val_entry(canvas, string_lit("Pass vertices"), fmt_write_scratch("{}", fmt_int(rendStats->passes[stats->passInspect].vertices)));
    stats_draw_val_entry(canvas, string_lit("Pass primitives"), fmt_write_scratch("{}", fmt_int(rendStats->passes[stats->passInspect].primitives)));
    stats_draw_val_entry(canvas, string_lit("Pass vertex-shaders"), fmt_write_scratch("{}", fmt_int(rendStats->passes[stats->passInspect].shadersVert)));
    stats_draw_val_entry(canvas, string_lit("Pass fragment-shaders"), fmt_write_scratch("{}", fmt_int(rendStats->passes[stats->passInspect].shadersFrag)));
  }
  if(stats_draw_section(canvas, string_lit("Memory"))) {
    const i64       pageDelta         = allocStats->pageCounter - statsGlobal->allocPrevPageCounter;
    const FormatArg pageDeltaColor    = pageDelta > 0 ? fmt_ui_color(ui_color_red) : fmt_nop();
    const i64       heapDelta         = allocStats->heapCounter - statsGlobal->allocPrevHeapCounter;
    const FormatArg heapDeltaColor    = heapDelta > 0 ? fmt_ui_color(ui_color_yellow) : fmt_nop();
    const i64       persistDelta      = allocStats->persistCounter - statsGlobal->allocPrevPersistCounter;
    const FormatArg persistDeltaColor = persistDelta > 0 ? fmt_ui_color(ui_color_red) : fmt_nop();

    stats_draw_val_entry(canvas, string_lit("Main"), fmt_write_scratch("{<11} pages: {}", fmt_size(allocStats->pageTotal), fmt_int(allocStats->pageCount)));
    stats_draw_val_entry(canvas, string_lit("Heap"), fmt_write_scratch("active: {}", fmt_int(allocStats->heapActive)));
    stats_draw_val_entry(canvas, string_lit("Heap counter"), fmt_write_scratch("count:  {<7} {}delta: {}\ar", fmt_int(allocStats->heapCounter), heapDeltaColor, fmt_int(heapDelta)));
    stats_draw_val_entry(canvas, string_lit("Page counter"), fmt_write_scratch("count:  {<7} {}delta: {}\ar", fmt_int(allocStats->pageCounter), pageDeltaColor, fmt_int(pageDelta)));
    stats_draw_val_entry(canvas, string_lit("Persist counter"), fmt_write_scratch("count:  {<7} {}delta: {}\ar", fmt_int(allocStats->persistCounter), persistDeltaColor, fmt_int(persistDelta)));
    stats_draw_val_entry(canvas, string_lit("Renderer chunks"), fmt_write_scratch("{}", fmt_int(rendStats->memChunks)));
    stats_draw_val_entry(canvas, string_lit("Renderer"), fmt_write_scratch("{<8} reserved: {}", fmt_size(rendStats->ramOccupied), fmt_size(rendStats->ramReserved)));
    stats_draw_val_entry(canvas, string_lit("GPU (on device)"), fmt_write_scratch("{<8} reserved: {}", fmt_size(rendStats->vramOccupied), fmt_size(rendStats->vramReserved)));
    stats_draw_val_entry(canvas, string_lit("Handles"), fmt_write_scratch("file: {<2} dynlib: {}", fmt_int(statsGlobal->fileCount), fmt_int(statsGlobal->dynlibCount)));
    stats_draw_val_entry(canvas, string_lit("StringTable"), fmt_write_scratch("global: {}", fmt_int(statsGlobal->globalStringCount)));
  }
  if(stats_draw_section(canvas, string_lit("ECS"))) {
    const TimeDuration maxFlushTime = (TimeDuration)(debug_plot_max(&statsGlobal->ecsFlushDurUs) * (f64)time_microsecond);

    stats_draw_val_entry(canvas, string_lit("Components"), fmt_write_scratch("{}", fmt_int(ecs_def_comp_count(ecsDef))));
    stats_draw_val_entry(canvas, string_lit("Views"), fmt_write_scratch("{}", fmt_int(ecs_def_view_count(ecsDef))));
    stats_draw_val_entry(canvas, string_lit("Systems"), fmt_write_scratch("{}", fmt_int(ecs_def_system_count(ecsDef))));
    stats_draw_val_entry(canvas, string_lit("Modules"), fmt_write_scratch("{}", fmt_int(ecs_def_module_count(ecsDef))));
    stats_draw_val_entry(canvas, string_lit("Entities"), fmt_write_scratch("{}", fmt_int(ecsWorldStats->entityCount)));
    stats_draw_val_entry(canvas, string_lit("Archetypes"), fmt_write_scratch("{<8} empty:  {}", fmt_int(ecsWorldStats->archetypeCount), fmt_int(ecsWorldStats->archetypeEmptyCount)));
    stats_draw_val_entry(canvas, string_lit("Archetype data"), fmt_write_scratch("{<8} chunks: {}", fmt_size(ecsWorldStats->archetypeTotalSize), fmt_int(ecsWorldStats->archetypeTotalChunks)));
    stats_draw_val_entry(canvas, string_lit("Plan"), fmt_write_scratch("{<8} est:    {}", fmt_int(ecsRunnerStats->planCounter), fmt_duration(ecsRunnerStats->planEstSpan)));
    stats_draw_val_entry(canvas, string_lit("Flush duration"), fmt_write_scratch("{<8} max:    {}", fmt_duration(ecsRunnerStats->flushDurLast), fmt_duration(maxFlushTime)));
    stats_draw_val_entry(canvas, string_lit("Flush entities"), fmt_write_scratch("{}", fmt_int(ecsWorldStats->lastFlushEntities)));
  }
  if(stats_draw_section(canvas, string_lit("Collision"))) {
    stats_draw_val_entry(canvas, string_lit("Prim spheres"), fmt_write_scratch("{}", fmt_int(colStats->queryStats[GeoQueryStat_PrimSphereCount])));
    stats_draw_val_entry(canvas, string_lit("Prim capsules"), fmt_write_scratch("{}", fmt_int(colStats->queryStats[GeoQueryStat_PrimCapsuleCount])));
    stats_draw_val_entry(canvas, string_lit("Prim box-rotated"), fmt_write_scratch("{}", fmt_int(colStats->queryStats[GeoQueryStat_PrimBoxRotatedCount])));
    stats_draw_val_entry(canvas, string_lit("Query ray"), fmt_write_scratch("normal: {<5} fat: {}", fmt_int(colStats->queryStats[GeoQueryStat_QueryRayCount]), fmt_int(colStats->queryStats[GeoQueryStat_QueryRayFatCount])));
    stats_draw_val_entry(canvas, string_lit("Query all"), fmt_write_scratch("sphere: {<5} box: {}", fmt_int(colStats->queryStats[GeoQueryStat_QuerySphereAllCount]), fmt_int(colStats->queryStats[GeoQueryStat_QueryBoxAllCount])));
  }
  if(stats_draw_section(canvas, string_lit("Navigation"))) {
    stats_draw_nav_layer_dropdown(canvas, stats);
    const u32* navStats = scene_nav_grid_stats(navEnv, stats->navLayerInspect);
    stats_draw_val_entry(canvas, string_lit("Cells"), fmt_write_scratch("total: {<6} axis: {}", fmt_int(navStats[GeoNavStat_CellCountTotal]), fmt_int(navStats[GeoNavStat_CellCountAxis])));
    stats_draw_val_entry(canvas, string_lit("Grid data"), fmt_write_scratch("{}", fmt_size(navStats[GeoNavStat_GridDataSize])));
    stats_draw_val_entry(canvas, string_lit("Worker data"), fmt_write_scratch("{}", fmt_size(navStats[GeoNavStat_WorkerDataSize])));
    stats_draw_val_entry(canvas, string_lit("Blockers"), fmt_write_scratch("total: {<4} additions: {}", fmt_int(navStats[GeoNavStat_BlockerCount]), fmt_int(navStats[GeoNavStat_BlockerAddCount])));
    stats_draw_val_entry(canvas, string_lit("Occupants"), fmt_write_scratch("{}", fmt_int(navStats[GeoNavStat_OccupantCount])));
    stats_draw_val_entry(canvas, string_lit("Islands"), fmt_write_scratch("{<11} computes: {}", fmt_int(navStats[GeoNavStat_IslandCount]), fmt_int(navStats[GeoNavStat_IslandComputes])));
    stats_draw_val_entry(canvas, string_lit("Path count"), fmt_write_scratch("{<11} limiter: {}", fmt_int(navStats[GeoNavStat_PathCount]), fmt_int(navStats[GeoNavStat_PathLimiterCount])));
    stats_draw_val_entry(canvas, string_lit("Path output"), fmt_write_scratch("cells: {}", fmt_int(navStats[GeoNavStat_PathOutputCells])));
    stats_draw_val_entry(canvas, string_lit("Path iterations"), fmt_write_scratch("cells: {<4} enqueues: {}", fmt_int(navStats[GeoNavStat_PathItrCells]), fmt_int(navStats[GeoNavStat_PathItrEnqueues])));
    stats_draw_val_entry(canvas, string_lit("Find count"), fmt_write_scratch("{}", fmt_int(navStats[GeoNavStat_FindCount])));
    stats_draw_val_entry(canvas, string_lit("Find iterations"), fmt_write_scratch("cells: {<4} enqueues: {}", fmt_int(navStats[GeoNavStat_FindItrCells]), fmt_int(navStats[GeoNavStat_FindItrEnqueues])));
    stats_draw_val_entry(canvas, string_lit("Channel queries"), fmt_write_scratch("{}", fmt_int(navStats[GeoNavStat_ChannelQueries])));
    stats_draw_val_entry(canvas, string_lit("Blocker reachable"), fmt_write_scratch("queries: {}", fmt_int(navStats[GeoNavStat_BlockerReachableQueries])));
    stats_draw_val_entry(canvas, string_lit("Blocker closest"), fmt_write_scratch("queries: {}", fmt_int(navStats[GeoNavStat_BlockerClosestQueries])));
  }
  if(stats_draw_section(canvas, string_lit("Interface"))) {
    stats_draw_val_entry(canvas, string_lit("Canvas size"), fmt_write_scratch("{}x{}", fmt_float(uiStats->canvasSize.x, .maxDecDigits = 0), fmt_float(uiStats->canvasSize.y, .maxDecDigits = 0)));
    stats_draw_val_entry(canvas, string_lit("Canvasses"), fmt_write_scratch("{}", fmt_int(uiStats->canvasCount)));
    stats_draw_val_entry(canvas, string_lit("Tracked elements"), fmt_write_scratch("{}", fmt_int(uiStats->trackedElemCount)));
    stats_draw_val_entry(canvas, string_lit("Persistent elements"), fmt_write_scratch("{}", fmt_int(uiStats->persistElemCount)));
    stats_draw_val_entry(canvas, string_lit("Atoms"), fmt_write_scratch("{<8} overlay: {}", fmt_int(uiStats->atomCount), fmt_int(uiStats->atomOverlayCount)));
    stats_draw_val_entry(canvas, string_lit("Clip-rects"), fmt_write_scratch("{}", fmt_int(uiStats->clipRectCount)));
    stats_draw_val_entry(canvas, string_lit("Commands"), fmt_write_scratch("{}", fmt_int(uiStats->commandCount)));
  }
  // clang-format on
}

static void debug_stats_update(
    DebugStatsComp*               stats,
    const RendStatsComp*          rendStats,
    const RendSettingsGlobalComp* rendGlobalSettings,
    const SceneTimeComp*          time) {

  const TimeDuration prevFrameDur = stats->frameDur;
  stats->frameDur                 = time->realDelta;
  debug_avg_dur(&stats->frameDurAvg, stats->frameDur);
  stats->frameFreqAvg                 = 1.0f / (stats->frameDurAvg / (f32)time_second);
  const TimeDuration frameDurVariance = math_abs(stats->frameDur - prevFrameDur);
  debug_plot_add(&stats->frameDurVarianceUs, (f32)(frameDurVariance / (f64)time_microsecond));

  stats->frameDurDesired = rendGlobalSettings->limiterFreq
                               ? time_second / rendGlobalSettings->limiterFreq
                               : time_second / 60; // TODO: This assumes a 60 hz display.

  debug_avg_f32(&stats->rendWaitForGpuFrac, debug_frame_frac(stats, rendStats->waitForGpuDur));
  debug_avg_f32(&stats->rendPresAcqFrac, debug_frame_frac(stats, rendStats->presentAcquireDur));
  debug_avg_f32(&stats->rendPresEnqFrac, debug_frame_frac(stats, rendStats->presentEnqueueDur));
  debug_avg_f32(&stats->rendPresWaitFrac, debug_frame_frac(stats, rendStats->presentWaitDur));
  debug_avg_f32(&stats->rendLimiterFrac, debug_frame_frac(stats, rendStats->limiterDur));
  debug_avg_f32(&stats->gpuExecFrac, debug_frame_frac(stats, rendStats->gpuExecDur));
  for (RendPass pass = 0; pass != RendPass_Count; ++pass) {
    debug_avg_f32(
        &stats->gpuPassFrac[pass], debug_frame_frac(stats, rendStats->passes[pass].gpuExecDur));
  }
}

static void
debug_stats_global_update(DebugStatsGlobalComp* statsGlobal, const EcsRunnerStats* ecsRunnerStats) {

  const TimeReal oldestNotifToKeep = time_real_offset(time_real_clock(), -stats_notify_max_age);
  debug_notify_prune_older(statsGlobal, oldestNotifToKeep);

  statsGlobal->fileCount         = file_count();
  statsGlobal->dynlibCount       = dynlib_count();
  statsGlobal->globalStringCount = stringtable_count(g_stringtable);

  debug_plot_add(
      &statsGlobal->ecsFlushDurUs, (f32)(ecsRunnerStats->flushDurLast / (f64)time_microsecond));
}

ecs_view_define(GlobalView) {
  ecs_access_read(RendSettingsGlobalComp);
  ecs_access_read(SceneCollisionStatsComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(DebugStatsGlobalComp);
}

ecs_view_define(StatsCreateView) {
  ecs_access_with(GapWindowComp);
  ecs_access_with(SceneCameraComp); // Only track stats for windows with 3d content.
  ecs_access_without(DebugStatsComp);
}

ecs_view_define(StatsUpdateView) {
  ecs_access_write(DebugStatsComp);
  ecs_access_read(RendStatsComp);
  ecs_access_read(UiStatsComp);
}

ecs_view_define(CanvasWrite) { ecs_access_write(UiCanvasComp); }

ecs_system_define(DebugStatsCreateSys) {
  // Create a single global stats component.
  if (!ecs_world_has_t(world, ecs_world_global(world), DebugStatsGlobalComp)) {
    ecs_world_add_t(
        world,
        ecs_world_global(world),
        DebugStatsGlobalComp,
        .notifications = dynarray_create_t(g_allocHeap, DebugStatsNotification, 8));
  }

  // Create a stats component for each window with 3d content (so with a camera).
  EcsView* createView = ecs_world_view_t(world, StatsCreateView);
  for (EcsIterator* itr = ecs_view_itr(createView); ecs_view_walk(itr);) {
    ecs_world_add_t(world, ecs_view_entity(itr), DebugStatsComp);
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
  const SceneNavEnvComp*         navEnv      = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const RendSettingsGlobalComp*  rendGlobalSet = ecs_view_read_t(globalItr, RendSettingsGlobalComp);

  const AllocStats     allocStats     = alloc_stats_query();
  const EcsWorldStats  ecsWorldStats  = ecs_world_stats_query(world);
  const EcsRunnerStats ecsRunnerStats = ecs_runner_stats_query(g_ecsRunningRunner);
  debug_stats_global_update(statsGlobal, &ecsRunnerStats);

  EcsIterator* canvasItr = ecs_view_itr(ecs_world_view_t(world, CanvasWrite));

  EcsView* statsView = ecs_world_view_t(world, StatsUpdateView);
  for (EcsIterator* itr = ecs_view_itr(statsView); ecs_view_walk(itr);) {
    DebugStatsComp*      stats     = ecs_view_write_t(itr, DebugStatsComp);
    const RendStatsComp* rendStats = ecs_view_read_t(itr, RendStatsComp);
    const UiStatsComp*   uiStats   = ecs_view_read_t(itr, UiStatsComp);
    const EcsDef*        ecsDef    = ecs_world_def(world);

    // Update statistics.
    debug_stats_update(stats, rendStats, rendGlobalSet, time);

    // Create or destroy the interface canvas as needed.
    if (stats->show != DebugStatShow_None && !stats->canvas) {
      stats->canvas = ui_canvas_create(world, ecs_view_entity(itr), UiCanvasCreateFlags_ToBack);
    } else if (stats->show == DebugStatShow_None && stats->canvas) {
      ecs_world_entity_destroy(world, stats->canvas);
      stats->canvas = 0;
    }

    // Draw the interface.
    if (stats->canvas && ecs_view_maybe_jump(canvasItr, stats->canvas)) {
      UiCanvasComp* canvas = ecs_view_write_t(canvasItr, UiCanvasComp);
      ui_canvas_reset(canvas);
      debug_stats_draw_interface(
          canvas,
          statsGlobal,
          stats,
          rendStats,
          &allocStats,
          ecsDef,
          &ecsWorldStats,
          &ecsRunnerStats,
          colStats,
          navEnv,
          uiStats);
    }
  }

  statsGlobal->allocPrevPageCounter    = allocStats.pageCounter;
  statsGlobal->allocPrevHeapCounter    = allocStats.heapCounter;
  statsGlobal->allocPrevPersistCounter = allocStats.persistCounter;
}

ecs_module_init(debug_stats_module) {
  ecs_register_comp(DebugStatsComp);
  ecs_register_comp(DebugStatsGlobalComp, .destructor = ecs_destruct_stats_global);

  ecs_register_view(GlobalView);
  ecs_register_view(StatsCreateView);
  ecs_register_view(StatsUpdateView);
  ecs_register_view(CanvasWrite);

  ecs_register_system(DebugStatsCreateSys, ecs_view_id(StatsCreateView));
  ecs_register_system(
      DebugStatsUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(StatsUpdateView),
      ecs_view_id(CanvasWrite));
}

void debug_stats_notify(DebugStatsGlobalComp* comp, const String key, const String value) {
  DebugStatsNotification* notif = debug_notify_get(comp, key);
  notif->timestamp              = time_real_clock();
  notif->valueLength            = math_min((u8)value.size, stats_notify_max_value_size);
  mem_cpy(mem_create(notif->value, notif->valueLength), string_slice(value, 0, notif->valueLength));
}

DebugStatShow debug_stats_show(const DebugStatsComp* comp) { return comp->show; }

void debug_stats_show_set(DebugStatsComp* comp, const DebugStatShow show) { comp->show = show; }
