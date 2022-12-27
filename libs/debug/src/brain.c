#include "ai_tracer_record.h"
#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "debug_brain.h"
#include "debug_register.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "scene_brain.h"
#include "scene_selection.h"
#include "script_mem.h"
#include "ui.h"

typedef enum {
  DebugBrainTab_Evaluation,
  DebugBrainTab_Memory,

  DebugBrainTab_Count,
} DebugBrainTab;

static const String g_brainTabNames[] = {
    string_static("Evaluation"),
    string_static("\uE322 Memory"),
};
ASSERT(array_elems(g_brainTabNames) == DebugBrainTab_Count, "Incorrect number of names");

typedef struct {
  StringHash key;
  String     name;
} DebugMemoryEntry;

ecs_comp_define(DebugBrainPanelComp) {
  UiPanel      panel;
  bool         hideEmptyMemory;
  UiScrollview scrollview;
};

static i8 memory_compare_entry_name(const void* a, const void* b) {
  return compare_string(field_ptr(a, DebugMemoryEntry, name), field_ptr(b, DebugMemoryEntry, name));
}

ecs_view_define(SubjectView) { ecs_access_write(SceneBrainComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

static void evaluation_options_draw(UiCanvasComp* canvas, EcsWorld* world, SceneBrainComp* brain) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 110);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);

  bool pauseEval = (scene_brain_flags(brain) & SceneBrainFlags_PauseEvaluation) != 0;
  ui_label(canvas, string_lit("Pause eval:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseEval)) {
    scene_brain_flags_toggle(brain, SceneBrainFlags_PauseEvaluation);
  }
  ui_table_next_column(canvas, &table);

  const EcsEntityId behavior = scene_brain_behavior(brain);
  const String behaviorName  = asset_id(ecs_utils_read_t(world, AssetView, behavior, AssetComp));
  ui_label(canvas, fmt_write_scratch("[{}]", fmt_text(behaviorName)), .align = UiAlign_MiddleRight);

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

static bool memory_draw_bool(UiCanvasComp* canvas, ScriptVal* value) {
  bool valBool = script_get_bool(*value, false);
  if (ui_toggle(canvas, &valBool)) {
    *value = script_bool(valBool);
    return true;
  }
  return false;
}

static bool memory_draw_f64(UiCanvasComp* canvas, ScriptVal* value) {
  f64 valNumber = script_get_number(*value, 0);
  if (ui_numbox(canvas, &valNumber, .min = f64_min, .max = f64_max)) {
    *value = script_number(valNumber);
    return true;
  }
  return false;
}

static bool memory_draw_vector3(UiCanvasComp* canvas, ScriptVal* value) {
  static const f32 g_spacing = 10.0f;
  const UiAlign    align     = UiAlign_MiddleLeft;
  ui_layout_push(canvas);
  ui_layout_resize(canvas, align, ui_vector(1.0f / 3, 0), UiBase_Current, Ui_X);
  ui_layout_grow(canvas, align, ui_vector(2 * -g_spacing / 3, 0), UiBase_Absolute, Ui_X);

  GeoVector vec3 = script_get_vector3(*value, geo_vector(0));

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

  *value = script_vector3(vec3);
  return dirty;
}

static bool memory_draw_entity(UiCanvasComp* canvas, ScriptVal* value) {
  const EcsEntityId valEntity = script_get_entity(*value, 0);
  ui_label_entity(canvas, valEntity);
  return false;
}

static bool memory_draw_value(UiCanvasComp* canvas, ScriptVal* value) {
  switch (script_type(*value)) {
  case ScriptType_Null:
    ui_label(canvas, string_lit("< none >"));
    return false;
  case ScriptType_Number:
    return memory_draw_f64(canvas, value);
  case ScriptType_Bool:
    return memory_draw_bool(canvas, value);
  case ScriptType_Vector3:
    return memory_draw_vector3(canvas, value);
  case ScriptType_Entity:
    return memory_draw_entity(canvas, value);
  case ScriptType_Count:
    break;
  }
  return false;
}

static void
memory_options_draw(UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, SceneBrainComp* brain) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 135);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);
  ui_table_add_column(&table, UiTableColumn_Fixed, 155);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);
  ui_table_add_column(&table, UiTableColumn_Fixed, 105);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);

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

  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Hide empty:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->hideEmptyMemory);

  ui_layout_pop(canvas);
}

static void
memory_panel_tab_draw(UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsIterator* subject) {
  diag_assert(subject);

  SceneBrainComp* brain = ecs_view_write_t(subject, SceneBrainComp);

  memory_options_draw(canvas, panelComp, brain);
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
          {string_lit("Key"), string_lit("Memory key.")},
          {string_lit("Type"), string_lit("Memory value type.")},
          {string_lit("Value"), string_lit("Memory value.")},
      });

  // Collect the memory entries.
  DynArray         entries = dynarray_create_t(g_alloc_scratch, DebugMemoryEntry, 256);
  const ScriptMem* memory  = scene_brain_memory(brain);
  for (ScriptMemItr itr = script_mem_begin(memory); itr.key; itr = script_mem_next(memory, itr)) {
    const String name = stringtable_lookup(g_stringtable, itr.key);
    if (panelComp->hideEmptyMemory && !script_val_has(scene_brain_get(brain, itr.key))) {
      continue;
    }
    *dynarray_push_t(&entries, DebugMemoryEntry) = (DebugMemoryEntry){
        .key  = itr.key,
        .name = string_is_empty(name) ? string_lit("<unnamed>") : name,
    };
  }

  // Sort the memory entries.
  dynarray_sort(&entries, memory_compare_entry_name);

  // Draw the memory entries.
  const f32 totalHeight = ui_table_height(&table, (u32)entries.size);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);

  if (entries.size) {
    dynarray_for_t(&entries, DebugMemoryEntry, entry) {
      ScriptVal value = scene_brain_get(brain, entry->key);

      ui_table_next_row(canvas, &table);
      ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

      ui_label(canvas, entry->name, .selectable = true);
      ui_table_next_column(canvas, &table);

      ui_label(canvas, script_val_type_str(script_type(value)));
      ui_table_next_column(canvas, &table);

      if (memory_draw_value(canvas, &value)) {
        scene_brain_set(brain, entry->key, value);
      }
    }
  } else {
    ui_label(canvas, string_lit("Memory empty."), .align = UiAlign_MiddleCenter);
  }

  dynarray_destroy(&entries);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void brain_panel_draw(
    UiCanvasComp* canvas, DebugBrainPanelComp* panelComp, EcsWorld* world, EcsIterator* subject) {

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
      evaluation_panel_tab_draw(canvas, panelComp, world, subject);
      break;
    case DebugBrainTab_Memory:
      memory_panel_tab_draw(canvas, panelComp, subject);
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
      world,
      panelEntity,
      DebugBrainPanelComp,
      .panel           = ui_panel(.size = ui_vector(750, 500)),
      .hideEmptyMemory = true);
  return panelEntity;
}
