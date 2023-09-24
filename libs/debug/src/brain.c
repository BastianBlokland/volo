#include "ai_tracer_record.h"
#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "debug_brain.h"
#include "debug_register.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "scene_brain.h"
#include "scene_selection.h"
#include "ui.h"

typedef enum {
  DebugBrainTab_Evaluation,
  DebugBrainTab_Settings,

  DebugBrainTab_Count,
} DebugBrainTab;

static const String g_brainTabNames[] = {
    string_static("Evaluation"),
    string_static("Settings"),
};
ASSERT(array_elems(g_brainTabNames) == DebugBrainTab_Count, "Incorrect number of names");

ecs_comp_define(DebugBrainPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
};

ecs_view_define(SubjectView) { ecs_access_write(SceneBrainComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

static void evaluation_options_draw(UiCanvasComp* canvas, EcsWorld* world, SceneBrainComp* brain) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  const EcsEntityId behavior = scene_brain_behavior(brain);
  const String behaviorName  = asset_id(ecs_utils_read_t(world, AssetView, behavior, AssetComp));
  ui_label(canvas, fmt_write_scratch("[{}]", fmt_text(behaviorName)));

  ui_layout_pop(canvas);
}

static UiColor evaluation_node_bg_color(const AiResult result) {
  switch (result) {
  case AiResult_Running:
    return ui_color(64, 64, 16, 192);
  case AiResult_Success:
    return ui_color(16, 64, 16, 192);
  case AiResult_Failure:
    return ui_color(64, 16, 16, 192);
  }
  diag_crash();
}

static void evaluation_panel_tab_draw(
    UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsWorld* world, EcsIterator* subject) {
  diag_assert(subject);

  SceneBrainComp*       brain  = ecs_view_write_t(subject, SceneBrainComp);
  const AiTracerRecord* tracer = scene_brain_tracer(brain);
  if (!tracer) {
    scene_brain_flags_set(brain, SceneBrainFlags_Trace);
    return;
  }

  evaluation_options_draw(canvas, world, brain);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 400);
  ui_table_add_column(&table, UiTableColumn_Fixed, 175);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Name"), string_lit("Behavior node name.")},
          {string_lit("Type"), string_lit("Behavior node type.")},
          {string_lit("Result"), string_lit("Evaluation result.")},
      });

  const u32 nodeCount   = ai_tracer_record_count(tracer);
  const f32 totalHeight = ui_table_height(&table, nodeCount);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);

  for (u32 nodeIndex = 0; nodeIndex != nodeCount; ++nodeIndex) {
    const AssetAiNodeType type   = ai_tracer_record_type(tracer, nodeIndex);
    const AiResult        result = ai_tracer_record_result(tracer, nodeIndex);
    const u32             depth  = ai_tracer_record_depth(tracer, nodeIndex);
    String                name   = ai_tracer_record_name(tracer, nodeIndex);
    if (string_is_empty(name)) {
      name = fmt_write_scratch("[{}]", fmt_text(asset_behavior_type_str(type)));
    }

    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, evaluation_node_bg_color(result));

    ui_label(canvas, fmt_write_scratch("{}{}", fmt_padding(depth * 4), fmt_text(name)));
    ui_table_next_column(canvas, &table);

    ui_label(canvas, asset_behavior_type_str(type));
    ui_table_next_column(canvas, &table);

    ui_label(canvas, ai_result_str(result));
    ui_table_next_column(canvas, &table);
  }

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void evaluation_settings_tab_draw(UiCanvasComp* canvas, EcsIterator* subject) {
  diag_assert(subject);

  SceneBrainComp* brain = ecs_view_write_t(subject, SceneBrainComp);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 160);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  bool pauseEval = (scene_brain_flags(brain) & SceneBrainFlags_PauseEvaluation) != 0;
  ui_label(canvas, string_lit("Pause eval:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseEval)) {
    scene_brain_flags_toggle(brain, SceneBrainFlags_PauseEvaluation);
  }

  ui_table_next_row(canvas, &table);
  bool pauseSensors = (scene_brain_flags(brain) & SceneBrainFlags_PauseSensors) != 0;
  ui_label(canvas, string_lit("Pause sensors:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseSensors)) {
    scene_brain_flags_toggle(brain, SceneBrainFlags_PauseSensors);
  }

  ui_table_next_row(canvas, &table);
  bool pauseControllers = (scene_brain_flags(brain) & SceneBrainFlags_PauseControllers) != 0;
  ui_label(canvas, string_lit("Pause controllers:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseControllers)) {
    scene_brain_flags_toggle(brain, SceneBrainFlags_PauseControllers);
  }
}

static void brain_panel_draw(
    UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsWorld* world, EcsIterator* subject) {

  const String title = fmt_write_scratch("{} Brain Panel", fmt_ui_shape(Psychology));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title       = title,
      .tabNames    = g_brainTabNames,
      .tabCount    = DebugBrainTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  if (subject) {
    switch (panelComp->panel.activeTab) {
    case DebugBrainTab_Evaluation:
      evaluation_panel_tab_draw(canvas, panelComp, world, subject);
      break;
    case DebugBrainTab_Settings:
      evaluation_settings_tab_draw(canvas, subject);
      break;
    }
  } else {
    ui_label(canvas, string_lit("Select an entity with a brain."), .align = UiAlign_MiddleCenter);
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) { ecs_access_read(SceneSelectionComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugBrainPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugBrainUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneSelectionComp* selection = ecs_view_read_t(globalItr, SceneSelectionComp);

  EcsView*     subjectView = ecs_world_view_t(world, SubjectView);
  EcsIterator* subject     = ecs_view_maybe_at(subjectView, scene_selection_main(selection));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugBrainPanelComp* panelComp = ecs_view_write_t(itr, DebugBrainPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    brain_panel_draw(canvas, panelComp, world, subject);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_brain_module) {
  ecs_register_comp(DebugBrainPanelComp);

  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(SubjectView);
  ecs_register_view(AssetView);

  ecs_register_system(
      DebugBrainUpdatePanelSys,
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView),
      ecs_view_id(AssetView));
}

EcsEntityId debug_brain_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugBrainPanelComp, .panel = ui_panel(.size = ui_vector(750, 500)));
  return panelEntity;
}
