#include "ai_blackboard.h"
#include "ai_tracer_record.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "debug_brain.h"
#include "debug_register.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_brain.h"
#include "scene_selection.h"
#include "ui.h"

typedef enum {
  DebugBrainTab_Evaluation,
  DebugBrainTab_Blackboard,

  DebugBrainTab_Count,
} DebugBrainTab;

static const String g_brainTabNames[] = {
    string_static("Evaluation"),
    string_static("Blackboard"),
};
ASSERT(array_elems(g_brainTabNames) == DebugBrainTab_Count, "Incorrect number of names");

typedef struct {
  StringHash key;
  String     name;
} DebugBlackboardEntry;

ecs_comp_define(DebugBrainPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
};

static i8 blackboard_compare_entry_name(const void* a, const void* b) {
  return compare_string(
      field_ptr(a, DebugBlackboardEntry, name), field_ptr(b, DebugBlackboardEntry, name));
}

ecs_view_define(SubjectView) { ecs_access_write(SceneBrainComp); }

static void evaluation_options_draw(UiCanvasComp* canvas, SceneBrainComp* brain) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  ui_table_next_row(canvas, &table);
  bool pauseEval = (scene_brain_flags(brain) & SceneBrainFlags_PauseEvaluation) != 0;
  ui_label(canvas, string_lit("Pause:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseEval)) {
    scene_brain_flags_toggle(brain, SceneBrainFlags_PauseEvaluation);
  }

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
    UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsIterator* subject) {
  diag_assert(subject);

  SceneBrainComp*       brain  = ecs_view_write_t(subject, SceneBrainComp);
  const AiTracerRecord* tracer = scene_brain_tracer(brain);
  if (!tracer) {
    scene_brain_flags_set(brain, SceneBrainFlags_Trace);
    return;
  }

  evaluation_options_draw(canvas, brain);
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
    const AssetBehaviorType type   = ai_tracer_record_type(tracer, nodeIndex);
    const AiResult          result = ai_tracer_record_result(tracer, nodeIndex);
    const u32               depth  = ai_tracer_record_depth(tracer, nodeIndex);
    String                  name   = ai_tracer_record_name(tracer, nodeIndex);
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

static bool blackboard_draw_bool(UiCanvasComp* canvas, AiValue* value) {
  bool valBool = ai_value_get_bool(*value, false);
  if (ui_toggle(canvas, &valBool)) {
    *value = ai_value_bool(valBool);
    return true;
  }
  return false;
}

static bool blackboard_draw_f64(UiCanvasComp* canvas, AiValue* value) {
  f64 valNumber = ai_value_get_f64(*value, 0);
  if (ui_numbox(canvas, &valNumber, .min = f64_min, .max = f64_max)) {
    *value = ai_value_f64(valNumber);
    return true;
  }
  return false;
}

static bool blackboard_draw_vector3(UiCanvasComp* canvas, AiValue* value) {
  static const f32 g_spacing = 10.0f;
  const UiAlign    align     = UiAlign_MiddleLeft;
  ui_layout_push(canvas);
  ui_layout_resize(canvas, align, ui_vector(1.0f / 3, 0), UiBase_Current, Ui_X);
  ui_layout_grow(canvas, align, ui_vector(2 * -g_spacing / 3, 0), UiBase_Absolute, Ui_X);

  GeoVector vec3 = ai_value_get_vector3(*value, geo_vector(0));

  bool dirty = false;
  for (u8 comp = 0; comp != 3; ++comp) {
    f64 compVal = vec3.comps[comp];
    if (ui_numbox(canvas, &compVal, .min = f32_min, .max = f32_max)) {
      vec3.comps[comp] = (f32)compVal;
      dirty            = true;
    }
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
  ui_layout_pop(canvas);

  *value = ai_value_vector3(vec3);
  return dirty;
}

static bool blackboard_draw_time(UiCanvasComp* canvas, AiValue* value) {
  const TimeDuration valTime = ai_value_get_time(*value, time_seconds(0));
  ui_label(canvas, fmt_write_scratch("{}", fmt_duration(valTime)));
  return false;
}

static bool blackboard_draw_entity(UiCanvasComp* canvas, AiValue* value) {
  const EcsEntityId valEntity = ai_value_get_entity(*value, 0);
  ui_label_entity(canvas, valEntity);
  return false;
}

static bool blackboard_draw_value(UiCanvasComp* canvas, AiValue* value) {
  switch (ai_value_type(*value)) {
  case AiValueType_None:
    ui_label(canvas, string_lit("< none >"));
    return false;
  case AiValueType_f64:
    return blackboard_draw_f64(canvas, value);
  case AiValueType_Bool:
    return blackboard_draw_bool(canvas, value);
  case AiValueType_Vector3:
    return blackboard_draw_vector3(canvas, value);
  case AiValueType_Time:
    return blackboard_draw_time(canvas, value);
  case AiValueType_Entity:
    return blackboard_draw_entity(canvas, value);
  case AiValueType_Count:
    break;
  }
  return false;
}

static void blackboard_options_draw(UiCanvasComp* canvas, SceneBrainComp* brain) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 135);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 155);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  ui_table_next_row(canvas, &table);
  bool pauseSensors = (scene_brain_flags(brain) & SceneBrainFlags_PauseSensors) != 0;
  ui_label(canvas, string_lit("Pause sensors:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseSensors)) {
    scene_brain_flags_toggle(brain, SceneBrainFlags_PauseSensors);
  }

  ui_table_next_column(canvas, &table);
  bool pauseControllers = (scene_brain_flags(brain) & SceneBrainFlags_PauseControllers) != 0;
  ui_label(canvas, string_lit("Pause controllers:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseControllers)) {
    scene_brain_flags_toggle(brain, SceneBrainFlags_PauseControllers);
  }

  ui_layout_pop(canvas);
}

static void blackboard_panel_tab_draw(
    UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsIterator* subject) {
  diag_assert(subject);

  SceneBrainComp* brain = ecs_view_write_t(subject, SceneBrainComp);
  AiBlackboard*   bb    = scene_brain_blackboard_mutable(brain);

  blackboard_options_draw(canvas, brain);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Key"), string_lit("Knowledge key.")},
          {string_lit("Type"), string_lit("Knowledge type.")},
          {string_lit("Value"), string_lit("Knowledge value.")},
      });

  // Collect the blackboard entries.
  DynArray entries = dynarray_create_t(g_alloc_scratch, DebugBlackboardEntry, 256);
  for (AiBlackboardItr itr = ai_blackboard_begin(bb); itr.key; itr = ai_blackboard_next(bb, itr)) {
    const String name                                = stringtable_lookup(g_stringtable, itr.key);
    *dynarray_push_t(&entries, DebugBlackboardEntry) = (DebugBlackboardEntry){
        .key  = itr.key,
        .name = string_is_empty(name) ? string_lit("<unnamed>") : name,
    };
  }

  // Sort the blackboard entries.
  dynarray_sort(&entries, blackboard_compare_entry_name);

  // Draw the blackboard entries.
  const f32 totalHeight = ui_table_height(&table, (u32)entries.size);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);

  if (entries.size) {
    dynarray_for_t(&entries, DebugBlackboardEntry, entry) {
      AiValue value = ai_blackboard_get(bb, entry->key);

      ui_table_next_row(canvas, &table);
      ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

      ui_label(canvas, entry->name, .selectable = true);
      ui_table_next_column(canvas, &table);

      ui_label(canvas, ai_value_type_str(ai_value_type(value)));
      ui_table_next_column(canvas, &table);

      if (blackboard_draw_value(canvas, &value)) {
        ai_blackboard_set(bb, entry->key, value);
      }
    }
  } else {
    ui_label(
        canvas, string_lit("Blackboard has no knowledge entries."), .align = UiAlign_MiddleCenter);
  }

  dynarray_destroy(&entries);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void
brain_panel_draw(UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsIterator* subject) {
  const String title = fmt_write_scratch("{} Brain Panel", fmt_ui_shape(Psychology));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title    = title,
      .tabNames = g_brainTabNames,
      .tabCount = DebugBrainTab_Count);

  if (subject) {
    switch (panelComp->panel.activeTab) {
    case DebugBrainTab_Evaluation:
      evaluation_panel_tab_draw(canvas, panelComp, subject);
      break;
    case DebugBrainTab_Blackboard:
      blackboard_panel_tab_draw(canvas, panelComp, subject);
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
    brain_panel_draw(canvas, panelComp, subject);

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

  ecs_register_system(
      DebugBrainUpdatePanelSys,
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView));
}

EcsEntityId debug_brain_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugBrainPanelComp, .panel = ui_panel(.size = ui_vector(750, 500)));
  return panelEntity;
}
