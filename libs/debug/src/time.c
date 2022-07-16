#include "ecs_world.h"
#include "gap_window.h"
#include "scene_time.h"
#include "ui.h"

ecs_comp_define(DebugTimePanelComp) { UiPanel panel; };

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugTimePanelComp);
  ecs_access_write(UiCanvasComp);
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

static void
time_panel_draw(UiCanvasComp* canvas, DebugTimePanelComp* panelComp, const SceneTimeComp* time) {
  const String title = fmt_write_scratch("{} Time Panel", fmt_ui_shape(Timer));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  time_panel_stat(
      canvas,
      &table,
      string_lit("Time"),
      fmt_write_scratch(
          "{<8} ({})",
          fmt_duration(time->time),
          fmt_float(time->time / (f32)time_second, .minDecDigits = 3, .maxDecDigits = 3)));

  ui_table_next_row(canvas, &table);
  time_panel_stat(
      canvas,
      &table,
      string_lit("Delta"),
      fmt_write_scratch(
          "{<8} ({})",
          fmt_duration(time->delta),
          fmt_float(time->delta / (f32)time_second, .minDecDigits = 3, .maxDecDigits = 3)));

  ui_table_next_row(canvas, &table);
  time_panel_stat(
      canvas, &table, string_lit("Ticks"), fmt_write_scratch("{}", fmt_int(time->ticks)));

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugTimeUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugTimePanelComp* panelComp = ecs_view_write_t(itr, DebugTimePanelComp);
    UiCanvasComp*       canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    time_panel_draw(canvas, panelComp, time);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_time_module) {
  ecs_register_comp(DebugTimePanelComp);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugTimeUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(GlobalView));
}

EcsEntityId debug_time_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(world, panelEntity, DebugTimePanelComp, .panel = ui_panel(ui_vector(330, 220)));
  return panelEntity;
}
