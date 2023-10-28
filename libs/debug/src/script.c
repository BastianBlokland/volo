#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_process.h"
#include "core_stringtable.h"
#include "debug_register.h"
#include "debug_script.h"
#include "ecs_utils.h"
#include "log_logger.h"
#include "scene_knowledge.h"
#include "scene_script.h"
#include "scene_selection.h"
#include "script_mem.h"
#include "ui.h"

#define output_max_age time_seconds(60)

typedef enum {
  DebugScriptTab_Output,
  DebugScriptTab_Stats,
  DebugScriptTab_Memory,
  DebugScriptTab_Settings,

  DebugScriptTab_Count,
} DebugScriptTab;

static const String g_scriptTabNames[] = {
    string_static("Output"),
    string_static("\uE4FC Stats"),
    string_static("\uE322 Memory"),
    string_static("\uE8B8 Settings"),
};
ASSERT(array_elems(g_scriptTabNames) == DebugScriptTab_Count, "Incorrect number of names");

typedef struct {
  StringHash key;
  String     name;
} DebugMemoryEntry;

typedef struct {
  TimeReal    timestamp;
  EcsEntityId entity;
} DebugScriptOutput;

ecs_comp_define(DebugScriptTrackerComp) {
  DynArray entries; // DebugScriptOutput[]
};

ecs_comp_define(DebugScriptPanelComp) {
  UiPanel      panel;
  bool         hideNullMemory;
  UiScrollview scrollview;
};

static void ecs_destruct_script_tracker(void* data) {
  DebugScriptTrackerComp* comp = data;
  dynarray_destroy(&comp->entries);
}

static i8 memory_compare_entry_name(const void* a, const void* b) {
  return compare_string(field_ptr(a, DebugMemoryEntry, name), field_ptr(b, DebugMemoryEntry, name));
}

static void debug_launch_editor(const String path) {
#if defined(VOLO_WIN32)
  const String editorFile = string_lit("code-tunnel.exe");
#else
  const String editorFile = string_lit("code");
#endif
  const String editorArgs[] = {string_lit("--reuse-window"), path};
  Process* proc = process_create(g_alloc_heap, editorFile, editorArgs, array_elems(editorArgs), 0);
  const ProcessExitCode exitCode = process_block(proc);
  if (exitCode != 0) {
    log_e("Failed to start editor", log_param("code", fmt_int(exitCode)));
  }
  process_destroy(proc);
}

ecs_view_define(SubjectView) {
  ecs_access_write(SceneKnowledgeComp);
  ecs_access_maybe_write(SceneScriptComp);
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

static DebugScriptTrackerComp* output_tracker_create(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      DebugScriptTrackerComp,
      .entries = dynarray_create_t(g_alloc_heap, DebugScriptOutput, 64));
}

static void output_prune_older(DebugScriptTrackerComp* tracker, const TimeReal timestamp) {
  usize keepIndex = 0;
  for (; keepIndex != tracker->entries.size; ++keepIndex) {
    if (dynarray_at_t(&tracker->entries, keepIndex, DebugScriptOutput)->timestamp >= timestamp) {
      break;
    }
  }
  dynarray_remove(&tracker->entries, 0, keepIndex);
}

static void output_add_panic(
    DebugScriptTrackerComp* tracker,
    const EcsEntityId       entity,
    const TimeReal          time,
    const ScriptPanic*      panic) {
  *dynarray_push_t(&tracker->entries, DebugScriptOutput) = (DebugScriptOutput){
      .entity    = entity,
      .timestamp = time,
  };
  (void)panic;
}

static void output_query(DebugScriptTrackerComp* tracker, EcsView* subjectView) {
  const TimeReal now          = time_real_clock();
  const TimeReal oldestToKeep = time_real_offset(now, -output_max_age);
  output_prune_older(tracker, oldestToKeep);

  for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
    const EcsEntityId      entity         = ecs_view_entity(itr);
    const SceneScriptComp* scriptInstance = ecs_view_read_t(itr, SceneScriptComp);
    const ScriptPanic*     panic          = scene_script_panic(scriptInstance);
    if (panic) {
      output_add_panic(tracker, entity, now, panic);
    }
  }
}

static void output_panel_tab_draw(UiCanvasComp* canvas, EcsWorld* world) {
  (void)canvas;
  (void)world;
}

static void stats_panel_tab_draw(
    UiCanvasComp*           canvas,
    EcsWorld*               world,
    const AssetManagerComp* assetManager,
    EcsIterator*            subject) {
  diag_assert(subject);

  const SceneScriptComp* scriptInstance = ecs_view_write_t(subject, SceneScriptComp);
  if (!scriptInstance) {
    ui_label(canvas, string_lit("No statistics available."), .align = UiAlign_MiddleCenter);
    return;
  }

  const SceneScriptStats* stats             = scene_script_stats(scriptInstance);
  const EcsEntityId       scriptAssetEntity = scene_script_asset(scriptInstance);
  const AssetComp* scriptAsset = ecs_utils_read_t(world, AssetView, scriptAssetEntity, AssetComp);
  const String     scriptName  = asset_id(scriptAsset);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Fixed, 350);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Script:"));
  ui_table_next_column(canvas, &table);
  ui_label(canvas, fmt_write_scratch("{}", fmt_text(scriptName)), .selectable = true);

  DynString scriptPathStr = dynstring_create(g_alloc_scratch, usize_kibibyte);
  if (asset_path(assetManager, scriptAsset, &scriptPathStr)) {
    ui_table_next_column(canvas, &table);
    ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(150, 0), UiBase_Absolute, Ui_X);
    if (ui_button(canvas, .label = string_lit("Edit Script"))) {
      debug_launch_editor(dynstring_view(&scriptPathStr));
    }
  }
  dynstring_destroy(&scriptPathStr);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Expressions:"));
  ui_table_next_column(canvas, &table);
  ui_label(canvas, fmt_write_scratch("{}", fmt_int(stats->executedExprs)));

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Duration:"));
  ui_table_next_column(canvas, &table);
  ui_label(canvas, fmt_write_scratch("{}", fmt_duration(stats->executedDur)));
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

  if (dirty) {
    *value = script_vector3(vec3);
  }
  return dirty;
}

static bool memory_draw_quat(UiCanvasComp* canvas, ScriptVal* value) {
  static const f32 g_spacing = 10.0f;
  const UiAlign    align     = UiAlign_MiddleLeft;
  ui_layout_push(canvas);
  ui_layout_resize(canvas, align, ui_vector(1.0f / 4, 0), UiBase_Current, Ui_X);
  ui_layout_grow(canvas, align, ui_vector(3 * -g_spacing / 4, 0), UiBase_Absolute, Ui_X);

  GeoQuat quat = script_get_quat(*value, geo_quat_ident);

  for (u8 comp = 0; comp != 4; ++comp) {
    f64 compVal = quat.comps[comp];
    ui_numbox(canvas, &compVal);
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
  ui_layout_pop(canvas);

  return false; // Does not support editing.
}

static bool memory_draw_entity(UiCanvasComp* canvas, ScriptVal* value) {
  const EcsEntityId valEntity = script_get_entity(*value, ecs_entity_invalid);
  ui_label_entity(canvas, valEntity);
  return false;
}

static bool memory_draw_string(UiCanvasComp* canvas, ScriptVal* value) {
  ui_label(canvas, script_val_str_scratch(*value));
  return false;
}

static bool memory_draw_value(UiCanvasComp* canvas, ScriptVal* value) {
  switch (script_type(*value)) {
  case ScriptType_Null:
    ui_label(canvas, string_lit("< null >"));
    return false;
  case ScriptType_Number:
    return memory_draw_f64(canvas, value);
  case ScriptType_Bool:
    return memory_draw_bool(canvas, value);
  case ScriptType_Vector3:
    return memory_draw_vector3(canvas, value);
  case ScriptType_Quat:
    return memory_draw_quat(canvas, value);
  case ScriptType_Entity:
    return memory_draw_entity(canvas, value);
  case ScriptType_String:
    return memory_draw_string(canvas, value);
  case ScriptType_Count:
    break;
  }
  return false;
}

static void memory_options_draw(UiCanvasComp* canvas, DebugScriptPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 105);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Hide null:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->hideNullMemory);

  ui_layout_pop(canvas);
}

static void
memory_panel_tab_draw(UiCanvasComp* canvas, DebugScriptPanelComp* panelComp, EcsIterator* subject) {
  diag_assert(subject);

  SceneKnowledgeComp* knowledge = ecs_view_write_t(subject, SceneKnowledgeComp);
  ScriptMem*          memory    = scene_knowledge_memory_mut(knowledge);

  memory_options_draw(canvas, panelComp);
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
  DynArray entries = dynarray_create_t(g_alloc_scratch, DebugMemoryEntry, 256);
  for (ScriptMemItr itr = script_mem_begin(memory); itr.key; itr = script_mem_next(memory, itr)) {
    const String name = stringtable_lookup(g_stringtable, itr.key);
    if (panelComp->hideNullMemory && !script_val_has(script_mem_get(memory, itr.key))) {
      continue;
    }
    *dynarray_push_t(&entries, DebugMemoryEntry) = (DebugMemoryEntry){
        .key  = itr.key,
        .name = string_is_empty(name) ? string_lit("< unnamed >") : name,
    };
  }

  // Sort the memory entries.
  dynarray_sort(&entries, memory_compare_entry_name);

  // Draw the memory entries.
  const f32 totalHeight = ui_table_height(&table, (u32)entries.size);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);

  if (entries.size) {
    dynarray_for_t(&entries, DebugMemoryEntry, entry) {
      ScriptVal value = script_mem_get(memory, entry->key);

      ui_table_next_row(canvas, &table);
      ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

      ui_label(canvas, entry->name, .selectable = true);
      ui_table_next_column(canvas, &table);

      ui_label(canvas, script_val_type_str(script_type(value)));
      ui_table_next_column(canvas, &table);

      if (memory_draw_value(canvas, &value)) {
        script_mem_set(memory, entry->key, value);
      }
    }
  } else {
    ui_label(canvas, string_lit("Memory empty."), .align = UiAlign_MiddleCenter);
  }

  dynarray_destroy(&entries);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void settings_panel_tab_draw(UiCanvasComp* canvas, EcsIterator* subject) {
  diag_assert(subject);

  SceneScriptComp* scriptInstance = ecs_view_write_t(subject, SceneScriptComp);
  if (!scriptInstance) {
    ui_label(canvas, string_lit("No settings available."), .align = UiAlign_MiddleCenter);
    return;
  }

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 160);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  bool pauseEval = (scene_script_flags(scriptInstance) & SceneScriptFlags_PauseEvaluation) != 0;
  ui_label(canvas, string_lit("Pause evaluation:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseEval)) {
    scene_script_flags_toggle(scriptInstance, SceneScriptFlags_PauseEvaluation);
  }
}

static void script_panel_draw(
    UiCanvasComp*           canvas,
    DebugScriptPanelComp*   panelComp,
    EcsWorld*               world,
    const AssetManagerComp* assetManager,
    EcsIterator*            subject) {

  const String title = fmt_write_scratch("{} Script Panel", fmt_ui_shape(Description));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title       = title,
      .tabNames    = g_scriptTabNames,
      .tabCount    = DebugScriptTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  if (subject) {
    switch (panelComp->panel.activeTab) {
    case DebugScriptTab_Output:
      output_panel_tab_draw(canvas, world);
      break;
    case DebugScriptTab_Stats:
      stats_panel_tab_draw(canvas, world, assetManager, subject);
      break;
    case DebugScriptTab_Memory:
      memory_panel_tab_draw(canvas, panelComp, subject);
      break;
    case DebugScriptTab_Settings:
      settings_panel_tab_draw(canvas, subject);
      break;
    }
  } else {
    ui_label(canvas, string_lit("Select a scripted entity."), .align = UiAlign_MiddleCenter);
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_read(SceneSelectionComp);
  ecs_access_read(AssetManagerComp);
  ecs_access_maybe_write(DebugScriptTrackerComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugScriptPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugScriptUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DebugScriptTrackerComp* tracker = ecs_view_write_t(globalItr, DebugScriptTrackerComp);
  if (!tracker) {
    tracker = output_tracker_create(world);
  }

  const SceneSelectionComp* selection    = ecs_view_read_t(globalItr, SceneSelectionComp);
  const AssetManagerComp*   assetManager = ecs_view_read_t(globalItr, AssetManagerComp);

  EcsView*     subjectView = ecs_world_view_t(world, SubjectView);
  EcsIterator* subject     = ecs_view_maybe_at(subjectView, scene_selection_main(selection));

  output_query(tracker, subjectView);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugScriptPanelComp* panelComp = ecs_view_write_t(itr, DebugScriptPanelComp);
    UiCanvasComp*         canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    script_panel_draw(canvas, panelComp, world, assetManager, subject);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_script_module) {
  ecs_register_comp(DebugScriptPanelComp);
  ecs_register_comp(DebugScriptTrackerComp, .destructor = ecs_destruct_script_tracker);

  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(SubjectView);
  ecs_register_view(AssetView);

  ecs_register_system(
      DebugScriptUpdatePanelSys,
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView),
      ecs_view_id(AssetView));
}

EcsEntityId debug_script_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugScriptPanelComp,
      .panel          = ui_panel(.size = ui_vector(750, 500)),
      .hideNullMemory = true);
  return panelEntity;
}
