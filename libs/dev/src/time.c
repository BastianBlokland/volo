#include "core/format.h"
#include "core/math.h"
#include "dev/id.h"
#include "dev/panel.h"
#include "dev/stats.h"
#include "dev/time.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "input/manager.h"
#include "scene/time.h"
#include "ui/canvas.h"
#include "ui/layout.h"
#include "ui/panel.h"
#include "ui/shape.h"
#include "ui/style.h"
#include "ui/table.h"
#include "ui/widget.h"

ecs_comp_define(DevTimePanelComp) { UiPanel panel; };

ecs_view_define(GlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(DevStatsGlobalComp);
  ecs_access_write(SceneTimeSettingsComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevTimePanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevTimePanelComp);
  ecs_access_write(UiCanvasComp);
}

static void dev_time_notify_scale(DevStatsGlobalComp* stats, const f32 timeScale) {
  dev_stats_notify(
      stats,
      string_lit("Time scale"),
      fmt_write_scratch("{}", fmt_float(timeScale, .maxDecDigits = 2, .expThresholdNeg = 0)));
}

static void dev_time_notify_pause(DevStatsGlobalComp* stats, const bool pause) {
  dev_stats_notify(stats, string_lit("Time pause"), fmt_write_scratch("{}", fmt_bool(pause)));
}

static void
time_panel_stat(UiCanvasComp* canvas, UiTable* table, const String label, const String stat) {
  ui_label(canvas, label);
  ui_table_next_column(canvas, table);
  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);
  ui_label(canvas, stat);
  ui_canvas_draw_text(canvas, stat, 16, UiAlign_MiddleLeft, UiFlags_None);
  ui_style_pop(canvas);
}

static void time_panel_stat_dur(
    UiCanvasComp* canvas, UiTable* table, const String label, const TimeDuration dur) {
  time_panel_stat(
      canvas,
      table,
      label,
      fmt_write_scratch(
          "{<8} ({})",
          fmt_duration(dur, .minDecDigits = 1, .maxDecDigits = 1),
          fmt_float(dur / (f32)time_second, .minDecDigits = 3, .maxDecDigits = 3)));
}

static void time_panel_draw(
    UiCanvasComp*          canvas,
    DevStatsGlobalComp*    stats,
    DevTimePanelComp*      panelComp,
    const SceneTimeComp*   time,
    SceneTimeSettingsComp* timeSettings) {
  const String title = fmt_write_scratch("{} Time Panel", fmt_ui_shape(Timer));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const bool isPaused = (timeSettings->flags & SceneTimeFlags_Paused) != 0;

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Paused"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle_flag(canvas, (u32*)&timeSettings->flags, SceneTimeFlags_Paused)) {
    dev_time_notify_pause(stats, (timeSettings->flags & SceneTimeFlags_Paused) != 0);
  }
  if (isPaused) {
    ui_layout_push(canvas);
    ui_layout_inner(
        canvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(100, 25), UiBase_Absolute);
    if (ui_button(canvas, .label = string_lit("Step"))) {
      timeSettings->flags |= SceneTimeFlags_Step;
    }
    ui_layout_pop(canvas);
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Scale"));
  ui_table_next_column(canvas, &table);
  if (ui_slider(
          canvas, &timeSettings->scale, .max = 4, .flags = isPaused ? UiWidget_Disabled : 0)) {
    dev_time_notify_scale(stats, timeSettings->scale);
  }

  ui_table_next_row(canvas, &table);
  time_panel_stat_dur(canvas, &table, string_lit("Time"), time->time);

  ui_table_next_row(canvas, &table);
  time_panel_stat_dur(canvas, &table, string_lit("Real Time"), time->realTime);

  ui_table_next_row(canvas, &table);
  time_panel_stat_dur(canvas, &table, string_lit("Delta"), time->delta);

  ui_table_next_row(canvas, &table);
  time_panel_stat_dur(canvas, &table, string_lit("Real Delta"), time->realDelta);

  ui_table_next_row(canvas, &table);
  time_panel_stat(
      canvas, &table, string_lit("Ticks"), fmt_write_scratch("{}", fmt_int(time->ticks)));

  ui_table_next_row(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Defaults"))) {
    timeSettings->flags = SceneTimeFlags_None;
    timeSettings->scale = 1.0f;
    dev_time_notify_scale(stats, 1.0f);
    dev_time_notify_pause(stats, false);
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DevTimeUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DevStatsGlobalComp*     stats        = ecs_view_write_t(globalItr, DevStatsGlobalComp);
  const InputManagerComp* input        = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneTimeComp*    time         = ecs_view_read_t(globalItr, SceneTimeComp);
  SceneTimeSettingsComp*  timeSettings = ecs_view_write_t(globalItr, SceneTimeSettingsComp);

  if (input_triggered_hash(input, DevId_DevTimePauseToggle)) {
    timeSettings->flags ^= SceneTimeFlags_Paused;
    if (timeSettings->flags & SceneTimeFlags_Paused) {
      dev_time_notify_pause(stats, true);
    } else {
      dev_time_notify_pause(stats, false);
    }
  }
  if (input_triggered_hash(input, DevId_DevTimeScaleUp)) {
    timeSettings->scale += 0.1f;
    dev_time_notify_scale(stats, timeSettings->scale);
  }
  if (input_triggered_hash(input, DevId_DevTimeScaleDown)) {
    timeSettings->scale = math_max(0.0f, timeSettings->scale - 0.1f);
    dev_time_notify_scale(stats, timeSettings->scale);
  }
  if (input_triggered_hash(input, DevId_DevTimeStep)) {
    timeSettings->flags |= SceneTimeFlags_Step;
  }

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DevTimePanelComp* panelComp = ecs_view_write_t(itr, DevTimePanelComp);
    UiCanvasComp*     canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue;
    }
    time_panel_draw(canvas, stats, panelComp, time, timeSettings);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(dev_time_module) {
  ecs_register_comp(DevTimePanelComp);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(DevTimeUpdateSys, ecs_view_id(PanelUpdateView), ecs_view_id(GlobalView));
}

EcsEntityId
dev_time_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId panelEntity = dev_panel_create(world, window, type);
  DevTimePanelComp* timePanel   = ecs_world_add_t(
      world,
      panelEntity,
      DevTimePanelComp,
      .panel = ui_panel(.position = ui_vector(0.5f, 0.5f), .size = ui_vector(500, 250)));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&timePanel->panel);
  }

  return panelEntity;
}
