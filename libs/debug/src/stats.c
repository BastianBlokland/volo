#include "core_alloc.h"
#include "debug_stats.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "rend_stats.h"
#include "scene_time.h"
#include "ui.h"

typedef enum {
  DebugStatsFlags_Show = 1 << 0,
} DebugStatsFlags;

ecs_comp_define(DebugStatsComp) {
  DebugStatsFlags flags;
  EcsEntityId     canvas;
};

static void stats_draw_bg(UiCanvasComp* canvas) {
  ui_style_push(canvas);
  ui_style_color(canvas, ui_color(0, 0, 0, 150));
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
  ui_style_pop(canvas);
}

static void stats_draw_val_entry(UiCanvasComp* canvas, const String label, const String value) {
  static const f32 g_labelWidth = 160.0f;

  stats_draw_bg(canvas);

  // Draw label.
  ui_layout_push(canvas);
  {
    ui_layout_resize(canvas, UiAlign_BottomLeft, ui_vector(g_labelWidth, 0), UiBase_Absolute, Ui_X);
    ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);
    ui_label(canvas, label, .align = UiAlign_MiddleLeft);
  }
  ui_layout_pop(canvas);

  // Draw value.
  ui_layout_push(canvas);
  {
    ui_layout_grow(canvas, UiAlign_MiddleRight, ui_vector(-g_labelWidth, 0), UiBase_Absolute, Ui_X);
    ui_style_push(canvas);
    {
      ui_style_variation(canvas, UiVariation_Monospace);
      ui_style_weight(canvas, UiWeight_Bold);
      ui_label(canvas, value);
    }
    ui_style_pop(canvas);
  }
  ui_layout_pop(canvas);

  ui_layout_next(canvas, Ui_Down, 0);
}

static void debug_stats_draw_interface(
    UiCanvasComp* canvas, const RendStatsComp* rendStats, const SceneTimeComp* time) {

  ui_layout_move_to(canvas, UiBase_Container, UiAlign_TopLeft, Ui_XY);
  ui_layout_resize(canvas, UiAlign_TopLeft, ui_vector(450, 25), UiBase_Absolute, Ui_XY);

  // clang-format off

  stats_draw_val_entry(canvas, string_lit("Device"), fmt_write_scratch("{}", fmt_text(rendStats->gpuName)));
  stats_draw_val_entry(canvas, string_lit("Resolution"), fmt_write_scratch("{}x{}", fmt_int(rendStats->renderSize[0]), fmt_int(rendStats->renderSize[1])));
  stats_draw_val_entry(canvas, string_lit("Update time"), fmt_write_scratch("{}", fmt_duration(time->delta)));
  stats_draw_val_entry(canvas, string_lit("Limiter time"), fmt_write_scratch("{}", fmt_duration(rendStats->limiterTime)));
  stats_draw_val_entry(canvas, string_lit("Render time"), fmt_write_scratch("{}", fmt_duration(rendStats->renderTime)));
  stats_draw_val_entry(canvas, string_lit("Render wait-time"), fmt_write_scratch("{}", fmt_duration(rendStats->waitForRenderTime)));
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
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  EcsIterator* canvasItr = ecs_view_itr(ecs_world_view_t(world, CanvasWrite));

  EcsView* statsView = ecs_world_view_t(world, StatsUpdateView);
  for (EcsIterator* itr = ecs_view_itr(statsView); ecs_view_walk(itr);) {
    DebugStatsComp*      statsComp = ecs_view_write_t(itr, DebugStatsComp);
    const RendStatsComp* rendStats = ecs_view_read_t(itr, RendStatsComp);
    const SceneTimeComp* time      = ecs_view_read_t(globalItr, SceneTimeComp);

    // Create or destroy the interface canvas as needed.
    if (statsComp->flags & DebugStatsFlags_Show && !statsComp->canvas) {
      statsComp->canvas = ui_canvas_create(world, ecs_view_entity(itr));
    } else if (!(statsComp->flags & DebugStatsFlags_Show) && statsComp->canvas) {
      ecs_world_entity_destroy(world, statsComp->canvas);
      statsComp->canvas = 0;
    }

    // Draw the interface.
    if (statsComp->canvas && ecs_view_maybe_jump(canvasItr, statsComp->canvas)) {
      UiCanvasComp* canvas = ecs_view_write_t(canvasItr, UiCanvasComp);
      ui_canvas_reset(canvas);
      ui_canvas_to_back(canvas);
      debug_stats_draw_interface(canvas, rendStats, time);
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
