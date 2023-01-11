#include "core_alloc.h"
#include "core_array.h"
#include "core_format.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "debug_stats.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "geo_query.h"
#include "rend_settings.h"
#include "rend_stats.h"
#include "scene_collision.h"
#include "scene_nav.h"
#include "scene_time.h"
#include "ui.h"
#include "ui_stats.h"

static const f32 g_statsLabelWidth       = 220;
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

typedef enum {
  DebugStatsFlags_Show = 1 << 0,
} DebugStatsFlags;

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
  DebugStatsFlags flags;
  EcsEntityId     canvas;

  TimeDuration  frameDur;
  TimeDuration  frameDurAvg;
  DebugStatPlot frameDurVarianceUs; // In microseconds.
  f32           frameFreqAvg;
  TimeDuration  frameDurDesired;

  f32 gpuPassFrac[RendPass_Count];

  f32 rendWaitFrac, presAcqFrac, presEnqFrac, presWaitFrac, limiterFrac;
  f32 gpuRendFrac;
};

ecs_comp_define(DebugStatsGlobalComp) {
  DynArray notifications; // DebugStatsNotification[].

  u64 allocPrevPageCounter, allocPrevHeapCounter, allocPrevPersistCounter;
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
} StatGraphSection;

static void stats_draw_graph(
    UiCanvasComp*           canvas,
    const StatGraphSection* sections,
    const u32               sectionCount,
    const String            tooltip) {
  ui_style_push(canvas);
  ui_style_outline(canvas, 0);

  f32 t = 0;
  for (u32 i = 0; i != sectionCount; ++i) {
    const f32 frac = math_min(sections[i].frac, 1.0f - t);
    if (frac < 0.0) {
      break; // TODO: This can happen as values are averaged independently.
    }
    if (frac < f32_epsilon) {
      continue;
    }
    ui_layout_push(canvas);
    ui_layout_move(canvas, ui_vector(t, 0), UiBase_Current, Ui_X);
    ui_layout_resize(canvas, UiAlign_BottomLeft, ui_vector(frac, 0), UiBase_Current, Ui_X);
    ui_style_color(canvas, sections[i].color);
    ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
    ui_layout_pop(canvas);
    t += frac;
  }

  ui_canvas_id_block_next(canvas); // Compensate for the potentially fluctuating amount of sections.

  if (!string_is_empty(tooltip)) {
    const UiId id = ui_canvas_id_peek(canvas);
    ui_canvas_draw_glyph(canvas, UiShape_Empty, 0, UiFlags_Interactable); // Invisible rect.
    ui_tooltip(canvas, id, tooltip, .variation = UiVariation_Monospace);
  }
  ui_style_pop(canvas);
}

static void stats_draw_cpu_graph(
    UiCanvasComp* canvas, const DebugStatsComp* stats, const RendStatsComp* rendStats) {
  stats_draw_bg(canvas, DebugBgFlags_None);
  stats_draw_label(canvas, string_lit("CPU"));

  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_layout_grow(
      canvas, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);
  ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-2, -2), UiBase_Absolute, Ui_XY);

  const f32 busyFrac = 1.0f - stats->rendWaitFrac - stats->presAcqFrac - stats->presEnqFrac -
                       stats->presWaitFrac - stats->limiterFrac;

  const StatGraphSection sections[] = {
      {math_max(busyFrac, 0), ui_color(0, 128, 0, 178)},
      {stats->rendWaitFrac, ui_color(255, 0, 0, 178)},
      {stats->presAcqFrac, ui_color(128, 0, 128, 178)},
      {stats->presEnqFrac, ui_color(0, 0, 255, 178)},
      {stats->presWaitFrac, ui_color(0, 128, 128, 128)},
      {stats->limiterFrac, ui_color(128, 128, 128, 128)},
  };
  const String tooltip = fmt_write_scratch(
      "\a~red\a.bWait for gpu\ar:    {>8}\n"
      "\a~purple\a.bPresent acquire\ar: {>8}\n"
      "\a~blue\a.bPresent enqueue\ar: {>8}\n"
      "\a~teal\a.bPresent wait\ar:    {>8}\n"
      "\a.bLimiter\ar:         {>8}",
      fmt_duration(rendStats->waitForRenderDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendStats->presentAcquireDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendStats->presentEnqueueDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendStats->presentWaitDur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendStats->limiterDur, .minDecDigits = 1, .maxDecDigits = 1));
  stats_draw_graph(canvas, sections, array_elems(sections), tooltip);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
  ui_layout_next(canvas, Ui_Down, 0);
}

static void stats_draw_gpu_graph(
    UiCanvasComp* canvas, const DebugStatsComp* stats, const RendStatsComp* rendStats) {
  stats_draw_bg(canvas, DebugBgFlags_None);
  stats_draw_label(canvas, string_lit("GPU"));

  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_layout_grow(
      canvas, UiAlign_MiddleRight, ui_vector(-g_statsLabelWidth, 0), UiBase_Absolute, Ui_X);
  ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-2, -2), UiBase_Absolute, Ui_XY);

  const f32 idleFrac        = 1.0f - stats->gpuRendFrac;
  const f32 renderOtherFrac = stats->gpuRendFrac - stats->gpuPassFrac[RendPass_Geometry] -
                              stats->gpuPassFrac[RendPass_Post] -
                              stats->gpuPassFrac[RendPass_Shadow] -
                              stats->gpuPassFrac[RendPass_AmbientOcclusion];

  const StatGraphSection sections[] = {
      {stats->gpuPassFrac[RendPass_Geometry], ui_color(0, 128, 128, 178)},
      {stats->gpuPassFrac[RendPass_Shadow], ui_color(128, 0, 128, 178)},
      {stats->gpuPassFrac[RendPass_AmbientOcclusion], ui_color(128, 128, 0, 178)},
      {renderOtherFrac, ui_color(0, 128, 0, 178)},
      {stats->gpuPassFrac[RendPass_Post], ui_color(128, 0, 0, 178)},
      {math_max(idleFrac, 0), ui_color(128, 128, 128, 128)},
  };
  const String tooltip = fmt_write_scratch(
      "\a~teal\a.bGeometry\ar:         {>7}\n"
      "\a~purple\a.bShadow\ar:           {>7}\n"
      "\a~orange\a.bAmbientOcclusion\ar: {>7}\n"
      "\a~green\a.bForward\ar:          {>7}\n"
      "\a~maroon\a.bPost\ar:             {>7}\n"
      "\a.bTotal\ar:            {>7}",
      fmt_duration(rendStats->passes[RendPass_Geometry].dur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendStats->passes[RendPass_Shadow].dur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(
          rendStats->passes[RendPass_AmbientOcclusion].dur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendStats->passes[RendPass_Forward].dur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendStats->passes[RendPass_Post].dur, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_duration(rendStats->renderDur, .minDecDigits = 1, .maxDecDigits = 1));
  stats_draw_graph(canvas, sections, array_elems(sections), tooltip);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
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
    const EcsWorldStats*           ecsStats,
    const SceneCollisionStatsComp* colStats,
    const SceneNavStatsComp*       navStats,
    const UiStatsComp*             uiStats) {

  ui_layout_move_to(canvas, UiBase_Container, UiAlign_TopLeft, Ui_XY);
  ui_layout_resize(canvas, UiAlign_TopLeft, ui_vector(510, 25), UiBase_Absolute, Ui_XY);

  // clang-format off
  stats_draw_frametime(canvas, stats);
  stats_draw_cpu_graph(canvas, stats, rendStats);
  stats_draw_gpu_graph(canvas, stats, rendStats);
  stats_draw_notifications(canvas, statsGlobal);

  if(stats_draw_section(canvas, string_lit("Renderer"))) {
    stats_draw_val_entry(canvas, string_lit("Device"), fmt_write_scratch("{}", fmt_text(rendStats->gpuName)));
    stats_draw_val_entry(canvas, string_lit("Resolution geo"), fmt_write_scratch("{}x{}", fmt_int(rendStats->passes[RendPass_Geometry].size[0]), fmt_int(rendStats->passes[RendPass_Geometry].size[1])));
    stats_draw_val_entry(canvas, string_lit("Resolution fwd"), fmt_write_scratch("{}x{}", fmt_int(rendStats->passes[RendPass_Forward].size[0]), fmt_int(rendStats->passes[RendPass_Forward].size[1])));
    stats_draw_val_entry(canvas, string_lit("Resolution post"), fmt_write_scratch("{}x{}", fmt_int(rendStats->passes[RendPass_Post].size[0]), fmt_int(rendStats->passes[RendPass_Post].size[1])));
    stats_draw_val_entry(canvas, string_lit("Resolution ao"), fmt_write_scratch("{}x{}", fmt_int(rendStats->passes[RendPass_AmbientOcclusion].size[0]), fmt_int(rendStats->passes[RendPass_AmbientOcclusion].size[1])));
    stats_draw_val_entry(canvas, string_lit("Resolution shadow"), fmt_write_scratch("{}x{}", fmt_int(rendStats->passes[RendPass_Shadow].size[0]), fmt_int(rendStats->passes[RendPass_Shadow].size[1])));

    stats_draw_val_entry(canvas, string_lit("Draws"), fmt_write_scratch("geo: {<8} fwd: {}", fmt_int(rendStats->passes[RendPass_Geometry].draws), fmt_int(rendStats->passes[RendPass_Forward].draws)));
    stats_draw_val_entry(canvas, string_lit("Instances"), fmt_write_scratch("geo: {<8} fwd: {}", fmt_int(rendStats->passes[RendPass_Geometry].instances), fmt_int(rendStats->passes[RendPass_Forward].instances)));
    stats_draw_val_entry(canvas, string_lit("Vertices"), fmt_write_scratch("geo: {<8} fwd: {}", fmt_int(rendStats->passes[RendPass_Geometry].vertices), fmt_int(rendStats->passes[RendPass_Forward].vertices)));
    stats_draw_val_entry(canvas, string_lit("Primitives"), fmt_write_scratch("geo: {<8} fwd: {}", fmt_int(rendStats->passes[RendPass_Geometry].primitives), fmt_int(rendStats->passes[RendPass_Forward].primitives)));
    stats_draw_val_entry(canvas, string_lit("Vertex shaders"), fmt_write_scratch("geo: {<8} fwd: {}", fmt_int(rendStats->passes[RendPass_Geometry].shadersVert), fmt_int(rendStats->passes[RendPass_Forward].shadersVert)));
    stats_draw_val_entry(canvas, string_lit("Fragment shaders"), fmt_write_scratch("geo: {<8} fwd: {}", fmt_int(rendStats->passes[RendPass_Geometry].shadersFrag), fmt_int(rendStats->passes[RendPass_Forward].shadersFrag)));

    stats_draw_val_entry(canvas, string_lit("Attachments"), fmt_write_scratch("{<3} {}", fmt_int(rendStats->attachCount), fmt_size(rendStats->attachMemory)));
    stats_draw_val_entry(canvas, string_lit("Descriptor sets"), fmt_write_scratch("{<3} reserved: {}", fmt_int(rendStats->descSetsOccupied), fmt_int(rendStats->descSetsReserved)));
    stats_draw_val_entry(canvas, string_lit("Descriptor layouts"), fmt_write_scratch("{}", fmt_int(rendStats->descLayouts)));
    stats_draw_val_entry(canvas, string_lit("Graphic resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Graphic])));
    stats_draw_val_entry(canvas, string_lit("Shader resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Shader])));
    stats_draw_val_entry(canvas, string_lit("Mesh resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Mesh])));
    stats_draw_val_entry(canvas, string_lit("Texture resources"), fmt_write_scratch("{}", fmt_int(rendStats->resources[RendStatRes_Texture])));
  }
  if(stats_draw_section(canvas, string_lit("Memory"))) {
    const i64       pageDelta         = allocStats->pageCounter - statsGlobal->allocPrevPageCounter;
    const FormatArg pageDeltaColor    = pageDelta > 0 ? fmt_ui_color(ui_color_red) : fmt_nop();
    const i64       heapDelta         = allocStats->heapCounter - statsGlobal->allocPrevHeapCounter;
    const FormatArg heapDeltaColor    = heapDelta > 0 ? fmt_ui_color(ui_color_yellow) : fmt_nop();
    const i64       persistDelta      = allocStats->persistCounter - statsGlobal->allocPrevPersistCounter;
    const FormatArg persistDeltaColor = persistDelta > 0 ? fmt_ui_color(ui_color_red) : fmt_nop();

    stats_draw_val_entry(canvas, string_lit("Main"), fmt_write_scratch("{<11} pages: {}", fmt_size(allocStats->pageTotal), fmt_int(allocStats->pageCount)));
    stats_draw_val_entry(canvas, string_lit("Page counter"), fmt_write_scratch("count:  {<6} {}delta: {}\ar", fmt_int(allocStats->pageCounter), pageDeltaColor, fmt_int(pageDelta)));
    stats_draw_val_entry(canvas, string_lit("Heap"), fmt_write_scratch("active: {}", fmt_int(allocStats->heapActive)));
    stats_draw_val_entry(canvas, string_lit("Heap counter"), fmt_write_scratch("count:  {<6} {}delta: {}\ar", fmt_int(allocStats->heapCounter), heapDeltaColor, fmt_int(heapDelta)));
    stats_draw_val_entry(canvas, string_lit("Persist counter"), fmt_write_scratch("count:  {<6} {}delta: {}\ar", fmt_int(allocStats->persistCounter), persistDeltaColor, fmt_int(persistDelta)));
    stats_draw_val_entry(canvas, string_lit("Renderer"), fmt_write_scratch("{<8} reserved: {}", fmt_size(rendStats->ramOccupied), fmt_size(rendStats->ramReserved)));
    stats_draw_val_entry(canvas, string_lit("Renderer chunks"), fmt_write_scratch("{}", fmt_int(rendStats->memChunks)));
    stats_draw_val_entry(canvas, string_lit("GPU (on device)"), fmt_write_scratch("{<8} reserved: {}", fmt_size(rendStats->vramOccupied), fmt_size(rendStats->vramReserved)));
    stats_draw_val_entry(canvas, string_lit("StringTable"), fmt_write_scratch("global: {}", fmt_int(statsGlobal->globalStringCount)));
  }
  if(stats_draw_section(canvas, string_lit("ECS"))) {
    const TimeDuration maxFlushTime = (TimeDuration)(debug_plot_max(&statsGlobal->ecsFlushDurUs) * (f64)time_microsecond);

    stats_draw_val_entry(canvas, string_lit("Components"), fmt_write_scratch("{}", fmt_int(ecs_def_comp_count(ecsDef))));
    stats_draw_val_entry(canvas, string_lit("Views"), fmt_write_scratch("{}", fmt_int(ecs_def_view_count(ecsDef))));
    stats_draw_val_entry(canvas, string_lit("Systems"), fmt_write_scratch("{}", fmt_int(ecs_def_system_count(ecsDef))));
    stats_draw_val_entry(canvas, string_lit("Modules"), fmt_write_scratch("{}", fmt_int(ecs_def_module_count(ecsDef))));
    stats_draw_val_entry(canvas, string_lit("Entities"), fmt_write_scratch("{}", fmt_int(ecsStats->entityCount)));
    stats_draw_val_entry(canvas, string_lit("Archetypes"), fmt_write_scratch("{<8} empty:  {}", fmt_int(ecsStats->archetypeCount), fmt_int(ecsStats->archetypeEmptyCount)));
    stats_draw_val_entry(canvas, string_lit("Archetype data"), fmt_write_scratch("{<8} chunks: {}", fmt_size(ecsStats->archetypeTotalSize), fmt_int(ecsStats->archetypeTotalChunks)));
    stats_draw_val_entry(canvas, string_lit("Flush duration"), fmt_write_scratch("{<8} max:    {}", fmt_duration(ecsStats->lastFlushDur), fmt_duration(maxFlushTime)));
    stats_draw_val_entry(canvas, string_lit("Flush entities"), fmt_write_scratch("{}", fmt_int(ecsStats->lastFlushEntities)));
  }
  if(stats_draw_section(canvas, string_lit("Collision"))) {
    stats_draw_val_entry(canvas, string_lit("Prim spheres"), fmt_write_scratch("{}", fmt_int(colStats->queryStats[GeoQueryStat_PrimSphereCount])));
    stats_draw_val_entry(canvas, string_lit("Prim capsules"), fmt_write_scratch("{}", fmt_int(colStats->queryStats[GeoQueryStat_PrimCapsuleCount])));
    stats_draw_val_entry(canvas, string_lit("Prim box-rotated"), fmt_write_scratch("{}", fmt_int(colStats->queryStats[GeoQueryStat_PrimBoxRotatedCount])));
    stats_draw_val_entry(canvas, string_lit("Query ray"), fmt_write_scratch("normal: {<5} fat: {}", fmt_int(colStats->queryStats[GeoQueryStat_QueryRayCount]), fmt_int(colStats->queryStats[GeoQueryStat_QueryRayFatCount])));
    stats_draw_val_entry(canvas, string_lit("Query all"), fmt_write_scratch("sphere: {<5} frustum: {}", fmt_int(colStats->queryStats[GeoQueryStat_QuerySphereAllCount]), fmt_int(colStats->queryStats[GeoQueryStat_QueryFrustumAllCount])));
  }
  if(stats_draw_section(canvas, string_lit("Navigation"))) {
    stats_draw_val_entry(canvas, string_lit("Cells"), fmt_write_scratch("total: {<6} axis: {}", fmt_int(navStats->gridStats[GeoNavStat_CellCountTotal]), fmt_int(navStats->gridStats[GeoNavStat_CellCountAxis])));
    stats_draw_val_entry(canvas, string_lit("Grid data"), fmt_write_scratch("{}", fmt_size(navStats->gridStats[GeoNavStat_GridDataSize])));
    stats_draw_val_entry(canvas, string_lit("Worker data"), fmt_write_scratch("{}", fmt_size(navStats->gridStats[GeoNavStat_WorkerDataSize])));
    stats_draw_val_entry(canvas, string_lit("Blockers"), fmt_write_scratch("total: {<4} additions: {}", fmt_int(navStats->gridStats[GeoNavStat_BlockerCount]), fmt_int(navStats->gridStats[GeoNavStat_BlockerAddCount])));
    stats_draw_val_entry(canvas, string_lit("Occupants"), fmt_write_scratch("{}", fmt_int(navStats->gridStats[GeoNavStat_OccupantCount])));
    stats_draw_val_entry(canvas, string_lit("Islands"), fmt_write_scratch("{}", fmt_int(navStats->gridStats[GeoNavStat_IslandCount])));
    stats_draw_val_entry(canvas, string_lit("Path count"), fmt_write_scratch("{}", fmt_int(navStats->gridStats[GeoNavStat_PathCount])));
    stats_draw_val_entry(canvas, string_lit("Path output"), fmt_write_scratch("cells: {}", fmt_int(navStats->gridStats[GeoNavStat_PathOutputCells])));
    stats_draw_val_entry(canvas, string_lit("Path iterations"), fmt_write_scratch("cells: {<4} enqueues: {}", fmt_int(navStats->gridStats[GeoNavStat_PathItrCells]), fmt_int(navStats->gridStats[GeoNavStat_PathItrEnqueues])));
    stats_draw_val_entry(canvas, string_lit("Find count"), fmt_write_scratch("{}", fmt_int(navStats->gridStats[GeoNavStat_FindCount])));
    stats_draw_val_entry(canvas, string_lit("Find iterations"), fmt_write_scratch("cells: {<4} enqueues: {}", fmt_int(navStats->gridStats[GeoNavStat_FindItrCells]), fmt_int(navStats->gridStats[GeoNavStat_FindItrEnqueues])));
    stats_draw_val_entry(canvas, string_lit("Line query count"), fmt_write_scratch("{}", fmt_int(navStats->gridStats[GeoNavStat_LineQueryCount])));
    stats_draw_val_entry(canvas, string_lit("Blocker reachable"), fmt_write_scratch("queries: {}", fmt_int(navStats->gridStats[GeoNavStat_BlockerReachableQueries])));
  }
  if(stats_draw_section(canvas, string_lit("Interface"))) {
    stats_draw_val_entry(canvas, string_lit("Canvas size"), fmt_write_scratch("{}x{}", fmt_float(uiStats->canvasSize.x, .maxDecDigits = 0), fmt_float(uiStats->canvasSize.y, .maxDecDigits = 0)));
    stats_draw_val_entry(canvas, string_lit("Canvasses"), fmt_write_scratch("{}", fmt_int(uiStats->canvasCount)));
    stats_draw_val_entry(canvas, string_lit("Tracked elements"), fmt_write_scratch("{}", fmt_int(uiStats->trackedElemCount)));
    stats_draw_val_entry(canvas, string_lit("Persistent elements"), fmt_write_scratch("{}", fmt_int(uiStats->persistElemCount)));
    stats_draw_val_entry(canvas, string_lit("Glyphs"), fmt_write_scratch("{<8} overlay: {}", fmt_int(uiStats->glyphCount), fmt_int(uiStats->glyphOverlayCount)));
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

  const f32 ref = (f32)stats->frameDur;
  debug_avg_f32(&stats->rendWaitFrac, math_clamp_f32(rendStats->waitForRenderDur / ref, 0, 1));
  debug_avg_f32(&stats->presAcqFrac, math_clamp_f32(rendStats->presentAcquireDur / ref, 0, 1));
  debug_avg_f32(&stats->presEnqFrac, math_clamp_f32(rendStats->presentEnqueueDur / ref, 0, 1));
  debug_avg_f32(&stats->presWaitFrac, math_clamp_f32(rendStats->presentWaitDur / ref, 0, 1));
  debug_avg_f32(&stats->limiterFrac, math_clamp_f32(rendStats->limiterDur / ref, 0, 1));
  debug_avg_f32(&stats->gpuRendFrac, math_clamp_f32(rendStats->renderDur / ref, 0, 1));

  for (RendPass pass = 0; pass != RendPass_Count; ++pass) {
    debug_avg_f32(
        &stats->gpuPassFrac[pass], math_clamp_f32(rendStats->passes[pass].dur / ref, 0, 1));
  }
}

static void
debug_stats_global_update(DebugStatsGlobalComp* statsGlobal, const EcsWorldStats* ecsStats) {

  const TimeReal oldestNotifToKeep = time_real_offset(time_real_clock(), -stats_notify_max_age);
  debug_notify_prune_older(statsGlobal, oldestNotifToKeep);

  statsGlobal->globalStringCount = stringtable_count(g_stringtable);

  debug_plot_add(
      &statsGlobal->ecsFlushDurUs, (f32)(ecsStats->lastFlushDur / (f64)time_microsecond));
}

ecs_view_define(GlobalView) {
  ecs_access_read(RendSettingsGlobalComp);
  ecs_access_read(SceneCollisionStatsComp);
  ecs_access_read(SceneNavStatsComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(DebugStatsGlobalComp);
}

ecs_view_define(StatsCreateView) {
  ecs_access_with(GapWindowComp);
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
        .notifications = dynarray_create_t(g_alloc_heap, DebugStatsNotification, 8));
  }

  // Create a stats component for each window.
  EcsView* createView = ecs_world_view_t(world, StatsCreateView);
  for (EcsIterator* itr = ecs_view_itr(createView); ecs_view_walk(itr);) {
    ecs_world_add_t(world, ecs_view_entity(itr), DebugStatsComp, .flags = DebugStatsFlags_Show);
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
  const SceneNavStatsComp*       navStats    = ecs_view_read_t(globalItr, SceneNavStatsComp);
  const RendSettingsGlobalComp*  rendGlobalSet = ecs_view_read_t(globalItr, RendSettingsGlobalComp);

  const AllocStats    allocStats = alloc_stats_query();
  const EcsWorldStats ecsStats   = ecs_world_stats_query(world);
  debug_stats_global_update(statsGlobal, &ecsStats);

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
    if (stats->flags & DebugStatsFlags_Show && !stats->canvas) {
      stats->canvas = ui_canvas_create(world, ecs_view_entity(itr), UiCanvasCreateFlags_ToBack);
    } else if (!(stats->flags & DebugStatsFlags_Show) && stats->canvas) {
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
          &ecsStats,
          colStats,
          navStats,
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

bool debug_stats_show(const DebugStatsComp* comp) {
  return (comp->flags & DebugStatsFlags_Show) != 0;
}

void debug_stats_show_set(DebugStatsComp* comp, const bool show) {
  if (show) {
    comp->flags |= DebugStatsFlags_Show;
  } else {
    comp->flags &= ~DebugStatsFlags_Show;
  }
}
