#include "core_alloc.h"
#include "core_array.h"
#include "core_math.h"
#include "debug_stats.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "rend_stats.h"
#include "scene_time.h"
#include "ui.h"

static const f32 g_statsLabelWidth    = 160;
static const u8  g_statsBgAlpha       = 150;
static const f32 g_statsAverageWindow = 10;

#define stats_plot_size 32

typedef enum {
  DebugStatsFlags_Show = 1 << 0,
} DebugStatsFlags;

typedef struct {
  f32 values[stats_plot_size];
  u32 cur;
} DebugStatPlot;

ecs_comp_define(DebugStatsComp) {
  DebugStatsFlags flags;
  EcsEntityId     canvas;

  TimeDuration  frameDur;
  TimeDuration  frameDurAvg;
  DebugStatPlot frameDurVarianceUs; // In microseconds.
  f32           frameFreqAvg;

  TimeDuration gpuRenderDurAvg;
  TimeDuration cpuLimiterDurAvg, cpuRendWaitDurAvg, cpuSwapAcquireDurAvg, cpuSwapPresentDurAvg;
};

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

static f64 debug_avg(const f64 oldAvg, const f64 new) {
  static const f64 g_invAverageWindow = 1.0f / g_statsAverageWindow;
  return oldAvg + (new - oldAvg) * g_invAverageWindow;
}

static TimeDuration debug_avg_dur(const TimeDuration oldAvg, const TimeDuration new) {
  return (TimeDuration)debug_avg((f64)oldAvg, (f64) new);
}

static void stats_draw_bg(UiCanvasComp* canvas) {
  ui_style_push(canvas);
  ui_style_color(canvas, ui_color(0, 0, 0, g_statsBgAlpha));
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
  ui_label(canvas, value);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void stats_draw_val_entry(UiCanvasComp* canvas, const String label, const String value) {
  stats_draw_bg(canvas);
  stats_draw_label(canvas, label);
  stats_draw_value(canvas, value);
  ui_layout_next(canvas, Ui_Down, 0);
}

static void stats_draw_frametime(UiCanvasComp* canvas, const DebugStatsComp* stats) {
  FormatArg colorArg = fmt_nop();
  if (stats->frameDur > time_milliseconds(34)) {
    colorArg = fmt_ui_color(ui_color_red);
  } else if (stats->frameDur > time_milliseconds(17)) {
    colorArg = fmt_ui_color(ui_color_yellow);
  }
  const f32    varianceUs = debug_plot_max(&stats->frameDurVarianceUs);
  const String freqText   = fmt_write_scratch(
      "{}hz", fmt_float(stats->frameFreqAvg, .minDecDigits = 1, .maxDecDigits = 1));

  stats_draw_val_entry(
      canvas,
      string_lit("Frame time"),
      fmt_write_scratch(
          "{}{<8}{<8}{>7} var",
          colorArg,
          fmt_duration(stats->frameDurAvg, .minDecDigits = 1),
          fmt_text(freqText),
          fmt_duration((TimeDuration)(varianceUs * time_microsecond), .maxDecDigits = 0)));
}

typedef struct {
  f64     frac;
  UiColor color;
} StatGraphSection;

static void stats_draw_graph(
    UiCanvasComp*           canvas,
    const StatGraphSection* sections,
    const u32               sectionCount,
    const String            tooltip) {
  ui_style_push(canvas);
  ui_style_outline(canvas, 0);

  f64 t = 0;
  for (u32 i = 0; i != sectionCount; ++i) {
    const f64 frac = math_min(sections[i].frac, 1.0 - t);
    ui_layout_push(canvas);
    ui_layout_move(canvas, ui_vector((f32)t, 0), UiBase_Current, Ui_X);
    ui_layout_resize(canvas, UiAlign_BottomLeft, ui_vector((f32)frac, 0), UiBase_Current, Ui_X);
    ui_style_color(canvas, sections[i].color);
    ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
    ui_layout_pop(canvas);
    t += frac;
  }
  if (!string_is_empty(tooltip)) {
    const UiId id = ui_canvas_id_peek(canvas);
    ui_style_layer(canvas, UiLayer_Invisible);
    ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_Interactable);
    ui_tooltip(canvas, id, tooltip, .variation = UiVariation_Monospace);
  }
  ui_style_pop(canvas);
}

static void stats_draw_cpu_graph(UiCanvasComp* canvas, const DebugStatsComp* stats) {
  stats_draw_bg(canvas);
  stats_draw_label(canvas, string_lit("CPU"));

  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_layout_grow(
      canvas, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);
  ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-2, -2), UiBase_Absolute, Ui_XY);

  const f64 limiterFrac = math_clamp_f64(stats->cpuLimiterDurAvg / (f64)stats->frameDur, 0, 1);
  const f64 rendWaitDur = math_clamp_f64(stats->cpuRendWaitDurAvg / (f64)stats->frameDur, 0, 1);
  const f64 acquireFrac = math_clamp_f64(stats->cpuSwapAcquireDurAvg / (f64)stats->frameDur, 0, 1);
  const f64 presentFrac = math_clamp_f64(stats->cpuSwapPresentDurAvg / (f64)stats->frameDur, 0, 1);
  const f64 busyFrac =
      math_clamp_f64(1.0f - limiterFrac - rendWaitDur - acquireFrac - presentFrac, 0, 1);

  const StatGraphSection sections[] = {
      {busyFrac, ui_color(0, 128, 0, 178)},
      {rendWaitDur, ui_color(255, 0, 0, 178)},
      {acquireFrac, ui_color(128, 0, 128, 178)},
      {presentFrac, ui_color(0, 0, 255, 178)},
      {limiterFrac, ui_color(128, 128, 128, 128)},
  };
  const String tooltip = fmt_write_scratch(
      "\a~red\a.bWait for gpu\ar:      {<7}\n"
      "\a~purple\a.bSwapchain acquire\ar: {<7}\n"
      "\a~blue\a.bSwapchain present\ar: {<7}\n"
      "\a.bLimiter\ar:           {<7}",
      fmt_duration(stats->cpuRendWaitDurAvg, .minDecDigits = 1),
      fmt_duration(stats->cpuSwapAcquireDurAvg, .minDecDigits = 1),
      fmt_duration(stats->cpuSwapPresentDurAvg, .minDecDigits = 1),
      fmt_duration(stats->cpuLimiterDurAvg, .minDecDigits = 1));
  stats_draw_graph(canvas, sections, array_elems(sections), tooltip);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
  ui_layout_next(canvas, Ui_Down, 0);
}

static void stats_draw_gpu_graph(UiCanvasComp* canvas, const DebugStatsComp* stats) {
  stats_draw_bg(canvas);
  stats_draw_label(canvas, string_lit("GPU"));

  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_layout_grow(
      canvas, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);
  ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-2, -2), UiBase_Absolute, Ui_XY);

  const f64 renderFrac = math_clamp_f64(stats->gpuRenderDurAvg / (f64)stats->frameDur, 0, 1);
  const f64 idleFrac   = math_clamp_f64(1.0f - renderFrac, 0, 1);

  const StatGraphSection sections[] = {
      {renderFrac, ui_color(0, 128, 0, 178)},
      {idleFrac, ui_color(128, 128, 128, 128)},
  };
  const String tooltip = fmt_write_scratch(
      "\a~green\a.bRender\ar: {<7}", fmt_duration(stats->gpuRenderDurAvg, .minDecDigits = 1));
  stats_draw_graph(canvas, sections, array_elems(sections), tooltip);

  ui_layout_next(canvas, Ui_Down, 0);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
  ui_layout_next(canvas, Ui_Down, 0);
}

static void debug_stats_draw_interface(
    UiCanvasComp* canvas, const DebugStatsComp* stats, const RendStatsComp* rendStats) {

  ui_layout_move_to(canvas, UiBase_Container, UiAlign_TopLeft, Ui_XY);
  ui_layout_resize(canvas, UiAlign_TopLeft, ui_vector(450, 25), UiBase_Absolute, Ui_XY);

  // clang-format off

  stats_draw_val_entry(canvas, string_lit("Device"), fmt_write_scratch("{}", fmt_text(rendStats->gpuName)));
  stats_draw_val_entry(canvas, string_lit("Resolution"), fmt_write_scratch("{}x{}", fmt_int(rendStats->renderSize[0]), fmt_int(rendStats->renderSize[1])));
  stats_draw_frametime(canvas, stats);
  stats_draw_cpu_graph(canvas, stats);
  stats_draw_gpu_graph(canvas, stats);
  stats_draw_val_entry(canvas, string_lit("Draws"), fmt_write_scratch("{}", fmt_int(rendStats->draws)));
  stats_draw_val_entry(canvas, string_lit("Instances"), fmt_write_scratch("{}", fmt_int(rendStats->instances)));
  stats_draw_val_entry(canvas, string_lit("Vertices"), fmt_write_scratch("{}", fmt_int(rendStats->vertices)));
  stats_draw_val_entry(canvas, string_lit("Triangles"), fmt_write_scratch("{}", fmt_int(rendStats->primitives)));
  stats_draw_val_entry(canvas, string_lit("Vertex shaders"), fmt_write_scratch("{}", fmt_int(rendStats->shadersVert)));
  stats_draw_val_entry(canvas, string_lit("Fragment shaders"), fmt_write_scratch("{}", fmt_int(rendStats->shadersFrag)));
  stats_draw_val_entry(canvas, string_lit("Memory main"), fmt_write_scratch("{}", fmt_size(alloc_stats_total())));
  stats_draw_val_entry(canvas, string_lit("Memory renderer"), fmt_write_scratch("{<8} reserved: {}", fmt_size(rendStats->ramOccupied), fmt_size(rendStats->ramReserved)));
  stats_draw_val_entry(canvas, string_lit("Memory gpu"), fmt_write_scratch("{<8} reserved: {}", fmt_size(rendStats->vramOccupied), fmt_size(rendStats->vramReserved)));
  stats_draw_val_entry(canvas, string_lit("Descriptor sets"), fmt_write_scratch("{<8} reserved: {}", fmt_int(rendStats->descSetsOccupied), fmt_int(rendStats->descSetsReserved)));
  stats_draw_val_entry(canvas, string_lit("Descriptor layouts"), fmt_write_scratch("{}", fmt_int(rendStats->descLayouts)));
  stats_draw_val_entry(canvas, string_lit("Graphic resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Graphic])));
  stats_draw_val_entry(canvas, string_lit("Shader resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Shader])));
  stats_draw_val_entry(canvas, string_lit("Mesh resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Mesh])));
  stats_draw_val_entry(canvas, string_lit("Texture resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Texture])));

  // clang-format on
}

static void debug_stats_update(
    DebugStatsComp* stats, const RendStatsComp* rendStats, const SceneTimeComp* time) {

  const TimeDuration prevFrameDur     = stats->frameDur;
  stats->frameDur                     = time ? time->delta : time_second;
  stats->frameDurAvg                  = debug_avg_dur(stats->frameDurAvg, stats->frameDur);
  stats->frameFreqAvg                 = 1.0f / (stats->frameDurAvg / (f32)time_second);
  const TimeDuration frameDurVariance = math_abs(stats->frameDur - prevFrameDur);
  debug_plot_add(&stats->frameDurVarianceUs, (f32)(frameDurVariance / (f64)time_microsecond));

  stats->gpuRenderDurAvg   = debug_avg_dur(stats->gpuRenderDurAvg, rendStats->renderDur);
  stats->cpuLimiterDurAvg  = debug_avg_dur(stats->cpuLimiterDurAvg, rendStats->limiterDur);
  stats->cpuRendWaitDurAvg = debug_avg_dur(stats->cpuRendWaitDurAvg, rendStats->waitForRenderDur);
  stats->cpuSwapAcquireDurAvg =
      debug_avg_dur(stats->cpuSwapAcquireDurAvg, rendStats->swapchainAquireDur);
  stats->cpuSwapPresentDurAvg =
      debug_avg_dur(stats->cpuSwapPresentDurAvg, rendStats->swapchainPresentDur);
}

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(StatsCreateView) {
  ecs_access_with(GapWindowComp);
  ecs_access_without(DebugStatsComp);
}

ecs_view_define(StatsUpdateView) {
  ecs_access_write(DebugStatsComp);
  ecs_access_read(RendStatsComp);
}

ecs_view_define(CanvasWrite) { ecs_access_write(UiCanvasComp); }

ecs_system_define(DebugStatsCreateSys) {
  EcsView* createView = ecs_world_view_t(world, StatsCreateView);
  for (EcsIterator* itr = ecs_view_itr(createView); ecs_view_walk(itr);) {
    ecs_world_add_t(world, ecs_view_entity(itr), DebugStatsComp, .flags = DebugStatsFlags_Show);
  }
}

ecs_system_define(DebugStatsUpdateSys) {
  EcsView*             globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator*         globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  const SceneTimeComp* time       = globalItr ? ecs_view_read_t(globalItr, SceneTimeComp) : null;

  EcsIterator* canvasItr = ecs_view_itr(ecs_world_view_t(world, CanvasWrite));

  EcsView* statsView = ecs_world_view_t(world, StatsUpdateView);
  for (EcsIterator* itr = ecs_view_itr(statsView); ecs_view_walk(itr);) {
    DebugStatsComp*      stats     = ecs_view_write_t(itr, DebugStatsComp);
    const RendStatsComp* rendStats = ecs_view_read_t(itr, RendStatsComp);

    // Update statistics.
    debug_stats_update(stats, rendStats, time);

    // Create or destroy the interface canvas as needed.
    if (stats->flags & DebugStatsFlags_Show && !stats->canvas) {
      stats->canvas = ui_canvas_create(world, ecs_view_entity(itr));
    } else if (!(stats->flags & DebugStatsFlags_Show) && stats->canvas) {
      ecs_world_entity_destroy(world, stats->canvas);
      stats->canvas = 0;
    }

    // Draw the interface.
    if (stats->canvas && ecs_view_maybe_jump(canvasItr, stats->canvas)) {
      UiCanvasComp* canvas = ecs_view_write_t(canvasItr, UiCanvasComp);
      ui_canvas_reset(canvas);
      ui_canvas_to_back(canvas);
      debug_stats_draw_interface(canvas, stats, rendStats);
    }
  }
}

ecs_module_init(debug_stats_module) {
  ecs_register_comp(DebugStatsComp);

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

bool debug_stats_show(const DebugStatsComp* comp) {
  return (comp->flags & DebugStatsFlags_Show) != 0;
}

void debug_stats_show_set(DebugStatsComp* comp, const bool show) {
  if (show) {
    comp->flags ^= DebugStatsFlags_Show;
  } else {
    comp->flags &= ~DebugStatsFlags_Show;
  }
}
