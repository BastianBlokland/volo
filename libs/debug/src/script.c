#include "asset_manager.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_process.h"
#include "core_stringtable.h"
#include "debug_panel.h"
#include "debug_register.h"
#include "debug_script.h"
#include "debug_shape.h"
#include "debug_text.h"
#include "ecs_utils.h"
#include "gap_window.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"
#include "log_logger.h"
#include "scene_knowledge.h"
#include "scene_script.h"
#include "scene_set.h"
#include "script_mem.h"
#include "script_panic.h"
#include "ui.h"

#define output_max_age time_seconds(60)
#define output_max_message_size 64

ASSERT(output_max_message_size < u8_max, "Message length has to be storable in a 8 bits")

static const String g_tooltipOpenScript   = string_static("Open script in external editor.");
static const String g_tooltipSelectEntity = string_static("Select the entity.");

typedef enum {
  DebugScriptTab_Info,
  DebugScriptTab_Memory,
  DebugScriptTab_Output,

  DebugScriptTab_Count,
} DebugScriptTab;

static const String g_scriptTabNames[] = {
    string_static("Info"),
    string_static("\uE322 Memory"),
    string_static("Output"),
};
ASSERT(array_elems(g_scriptTabNames) == DebugScriptTab_Count, "Incorrect number of names");

typedef struct {
  StringHash key;
  String     name;
} DebugMemoryEntry;

typedef enum {
  DebugScriptOutputMode_All,
  DebugScriptOutputMode_Self,

  DebugScriptOutputMode_Count
} DebugScriptOutputMode;

static const String g_outputModeNames[] = {
    string_static("All"),
    string_static("Self"),
};
ASSERT(array_elems(g_outputModeNames) == DebugScriptOutputMode_Count, "Incorrect number of names");

typedef enum {
  DebugScriptOutputType_Trace,
  DebugScriptOutputType_Panic,
} DebugScriptOutputType;

typedef struct {
  DebugScriptOutputType type : 8;
  u8                    msgLength;
  TimeReal              timestamp;
  EcsEntityId           entity;
  String                scriptId; // NOTE: Has to be persistently allocated.
  ScriptRangeLineCol    range;
  u8                    msgData[output_max_message_size];
} DebugScriptOutput;

typedef struct {
  String           scriptId; // NOTE: Has to be persistently allocated.
  ScriptPosLineCol pos;
} DebugEditorRequest;

ecs_comp_define(DebugScriptTrackerComp) {
  DynArray entries; // DebugScriptOutput[]
  bool     autoOpenOnPanic;
};

ecs_comp_define(DebugScriptPanelComp) {
  UiPanel               panel;
  bool                  hideNullMemory;
  DebugScriptOutputMode outputMode;
  UiScrollview          scrollview;
  u32                   lastRowCount;
  DebugEditorRequest    editorReq;
  Process*              editorLaunch;
};

static void ecs_destruct_script_tracker(void* data) {
  DebugScriptTrackerComp* comp = data;
  dynarray_destroy(&comp->entries);
}

static void ecs_destroy_script_panel(void* data) {
  DebugScriptPanelComp* comp = data;
  if (comp->editorLaunch) {
    process_destroy(comp->editorLaunch);
  }
}

ecs_view_define(SubjectView) {
  ecs_access_write(SceneKnowledgeComp);
  ecs_access_maybe_write(SceneScriptComp);
}

ecs_view_define(AssetView) {
  ecs_access_read(AssetComp);
  ecs_access_maybe_read(AssetScriptComp); // Maybe-read because it could have been unloaded since.
}

ecs_view_define(WindowView) { ecs_access_with(GapWindowComp); }

static void info_panel_tab_draw(
    EcsWorld*             world,
    UiCanvasComp*         canvas,
    DebugScriptPanelComp* panelComp,
    EcsIterator*          assetItr,
    EcsIterator*          subjectItr) {
  diag_assert(subjectItr);

  SceneScriptComp* scriptInstance = ecs_view_write_t(subjectItr, SceneScriptComp);
  if (!scriptInstance) {
    ui_label(canvas, string_lit("No statistics available."), .align = UiAlign_MiddleCenter);
    return;
  }

  const SceneScriptStats* stats             = scene_script_stats(scriptInstance);
  const EcsEntityId       scriptAssetEntity = scene_script_asset(scriptInstance);
  ecs_view_jump(assetItr, scriptAssetEntity);
  const AssetComp* scriptAsset       = ecs_view_read_t(assetItr, AssetComp);
  const bool       scriptAssetError  = ecs_world_has_t(world, scriptAssetEntity, AssetFailedComp);
  const bool       scriptAssetLoaded = ecs_world_has_t(world, scriptAssetEntity, AssetLoadedComp);
  const String     scriptId          = asset_id(scriptAsset);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Script:"));
  ui_table_next_column(canvas, &table);
  ui_label(canvas, fmt_write_scratch("{}", fmt_text(scriptId)), .selectable = true);

  ui_layout_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(100, 25), UiBase_Absolute);
  if (ui_button(canvas, .label = string_lit("Open Script"), .tooltip = g_tooltipOpenScript)) {
    panelComp->editorReq = (DebugEditorRequest){.scriptId = scriptId};
  }
  ui_layout_pop(canvas);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Status:"));
  ui_table_next_column(canvas, &table);
  if (scriptAssetError) {
    ui_style_push(canvas);
    ui_style_color(canvas, ui_color_red);
    ui_label(canvas, string_lit("Invalid script"));
    ui_style_pop(canvas);
  } else {
    ui_label(canvas, scriptAssetLoaded ? string_lit("Running") : string_lit("Loading script"));
  }

  ui_table_next_row(canvas, &table);
  bool pauseEval = (scene_script_flags(scriptInstance) & SceneScriptFlags_PauseEvaluation) != 0;
  ui_label(canvas, string_lit("Pause:"));
  ui_table_next_column(canvas, &table);
  if (ui_toggle(canvas, &pauseEval)) {
    scene_script_flags_toggle(scriptInstance, SceneScriptFlags_PauseEvaluation);
  }

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

static bool memory_draw_num(UiCanvasComp* canvas, ScriptVal* value) {
  f64 valNumber = script_get_num(*value, 0);
  if (ui_numbox(canvas, &valNumber, .min = f64_min, .max = f64_max)) {
    *value = script_num(valNumber);
    return true;
  }
  return false;
}

static bool memory_draw_f32_values(UiCanvasComp* canvas, f32* values, const u32 valueCount) {
  static const f32 g_spacing = 10.0f;
  const UiAlign    align     = UiAlign_MiddleLeft;
  ui_layout_push(canvas);
  ui_layout_resize(canvas, align, ui_vector(1.0f / valueCount, 0), UiBase_Current, Ui_X);
  ui_layout_grow(canvas, align, ui_vector(2 * -g_spacing / valueCount, 0), UiBase_Absolute, Ui_X);

  bool dirty = false;
  for (u32 i = 0; i != valueCount; ++i) {
    f64 compVal = values[i];
    if (ui_numbox(canvas, &compVal, .min = f32_min, .max = f32_max)) {
      values[i] = (f32)compVal;
      dirty     = true;
    }
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
  ui_layout_pop(canvas);

  return dirty;
}

static bool memory_draw_vec3(UiCanvasComp* canvas, ScriptVal* value) {
  GeoVector vec3 = script_get_vec3(*value, geo_vector(0));
  if (memory_draw_f32_values(canvas, vec3.comps, 3)) {
    *value = script_vec3(vec3);
    return true;
  }
  return false;
}

static bool memory_draw_quat(UiCanvasComp* canvas, ScriptVal* value) {
  GeoQuat quat = script_get_quat(*value, geo_quat_ident);
  if (memory_draw_f32_values(canvas, quat.comps, 4)) {
    *value = script_quat(quat);
    return true;
  }
  return false;
}

static bool memory_draw_color(UiCanvasComp* canvas, ScriptVal* value) {
  GeoColor col = script_get_color(*value, geo_color_clear);
  if (memory_draw_f32_values(canvas, col.data, 4)) {
    *value = script_color(col);
    return true;
  }
  return false;
}

static bool memory_draw_entity(UiCanvasComp* canvas, ScriptVal* value) {
  const EcsEntityId valEntity = script_get_entity(*value, ecs_entity_invalid);
  ui_label_entity(canvas, valEntity);
  return false;
}

static bool memory_draw_str(UiCanvasComp* canvas, ScriptVal* value) {
  ui_label(canvas, script_val_scratch(*value));
  return false;
}

static bool memory_draw_val(UiCanvasComp* canvas, ScriptVal* value) {
  switch (script_type(*value)) {
  case ScriptType_Null:
    ui_label(canvas, string_lit("< null >"));
    return false;
  case ScriptType_Num:
    return memory_draw_num(canvas, value);
  case ScriptType_Bool:
    return memory_draw_bool(canvas, value);
  case ScriptType_Vec3:
    return memory_draw_vec3(canvas, value);
  case ScriptType_Quat:
    return memory_draw_quat(canvas, value);
  case ScriptType_Color:
    return memory_draw_color(canvas, value);
  case ScriptType_Entity:
    return memory_draw_entity(canvas, value);
  case ScriptType_Str:
    return memory_draw_str(canvas, value);
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

static i8 memory_compare_entry_name(const void* a, const void* b) {
  return compare_string(field_ptr(a, DebugMemoryEntry, name), field_ptr(b, DebugMemoryEntry, name));
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

  DynArray entries = dynarray_create_t(g_alloc_scratch, DebugMemoryEntry, 256);
  for (ScriptMemItr itr = script_mem_begin(memory); itr.key; itr = script_mem_next(memory, itr)) {
    const String name = stringtable_lookup(g_stringtable, itr.key);
    if (panelComp->hideNullMemory && !script_val_has(script_mem_load(memory, itr.key))) {
      continue;
    }
    *dynarray_push_t(&entries, DebugMemoryEntry) = (DebugMemoryEntry){
        .key  = itr.key,
        .name = string_is_empty(name) ? string_lit("< unnamed >") : name,
    };
  }

  dynarray_sort(&entries, memory_compare_entry_name);

  const f32 totalHeight = ui_table_height(&table, (u32)entries.size);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);

  if (entries.size) {
    dynarray_for_t(&entries, DebugMemoryEntry, entry) {
      ScriptVal value = script_mem_load(memory, entry->key);

      ui_table_next_row(canvas, &table);
      ui_table_draw_row_bg(canvas, &table, ui_color(48, 48, 48, 192));

      ui_label(canvas, entry->name, .selectable = true);
      ui_table_next_column(canvas, &table);

      ui_label(canvas, script_val_type_str(script_type(value)));
      ui_table_next_column(canvas, &table);

      if (memory_draw_val(canvas, &value)) {
        script_mem_store(memory, entry->key, value);
      }
    }
  } else {
    ui_label(canvas, string_lit("Memory empty."), .align = UiAlign_MiddleCenter);
  }

  dynarray_destroy(&entries);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static DebugScriptTrackerComp* output_tracker_create(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      DebugScriptTrackerComp,
      .entries         = dynarray_create_t(g_alloc_heap, DebugScriptOutput, 64),
      .autoOpenOnPanic = true);
}

static bool output_has_panic(const DebugScriptTrackerComp* tracker) {
  dynarray_for_t(&tracker->entries, DebugScriptOutput, entry) {
    if (entry->type == DebugScriptOutputType_Panic) {
      return true;
    }
  }
  return false;
}

static void output_clear(DebugScriptTrackerComp* tracker) { dynarray_clear(&tracker->entries); }

static void output_prune_older(DebugScriptTrackerComp* tracker, const TimeReal timestamp) {
  for (usize i = tracker->entries.size; i-- != 0;) {
    if (dynarray_at_t(&tracker->entries, i, DebugScriptOutput)->timestamp < timestamp) {
      dynarray_remove_unordered(&tracker->entries, i, 1);
    }
  }
}

static void output_add(
    DebugScriptTrackerComp*     tracker,
    const DebugScriptOutputType type,
    const EcsEntityId           entity,
    const TimeReal              time,
    const String                scriptId,
    const String                message,
    const ScriptRangeLineCol    range) {
  DebugScriptOutput* entry = null;
  // Find an existing entry of the same type for the same entity.
  for (usize i = 0; i != tracker->entries.size; ++i) {
    DebugScriptOutput* existingEntry = dynarray_at_t(&tracker->entries, i, DebugScriptOutput);
    if (existingEntry->type == type && existingEntry->entity == entity) {
      entry = existingEntry;
      break;
    }
  }
  if (!entry) {
    // No existing entry found; add a new one.
    entry = dynarray_push_t(&tracker->entries, DebugScriptOutput);
  }
  entry->type      = type;
  entry->msgLength = math_min((u8)message.size, output_max_message_size);
  entry->timestamp = time;
  entry->entity    = entity;
  entry->scriptId  = scriptId;
  entry->range     = range;
  mem_cpy(mem_create(entry->msgData, entry->msgLength), string_slice(message, 0, entry->msgLength));
}

static void
output_query(DebugScriptTrackerComp* tracker, EcsIterator* assetItr, EcsView* subjectView) {
  const TimeReal now          = time_real_clock();
  const TimeReal oldestToKeep = time_real_offset(now, -output_max_age);
  output_prune_older(tracker, oldestToKeep);

  for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
    const EcsEntityId      entity         = ecs_view_entity(itr);
    const SceneScriptComp* scriptInstance = ecs_view_read_t(itr, SceneScriptComp);
    if (!scriptInstance) {
      continue;
    }
    ecs_view_jump(assetItr, scene_script_asset(scriptInstance));
    const AssetComp*       assetComp  = ecs_view_read_t(assetItr, AssetComp);
    const AssetScriptComp* scriptComp = ecs_view_read_t(assetItr, AssetScriptComp);
    const String           scriptId   = asset_id(assetComp);

    // Output panics.
    const ScriptPanic* panic = scene_script_panic(scriptInstance);
    if (panic) {
      const String       msg   = script_panic_kind_str(panic->kind);
      ScriptRangeLineCol range = {0};
      if (scriptComp) {
        range = script_range_to_line_col(scriptComp->sourceText, panic->range);
      }
      output_add(tracker, DebugScriptOutputType_Panic, entity, now, scriptId, msg, range);
    }

    // Output traces.
    const SceneScriptDebug* debugData  = scene_script_debug_data(scriptInstance);
    const usize             debugCount = scene_script_debug_count(scriptInstance);
    for (usize i = 0; i != debugCount; ++i) {
      if (debugData[i].type == SceneScriptDebugType_Trace) {
        const String             msg   = debugData[i].data_trace.text;
        const ScriptRangeLineCol range = {0}; // TODO: Collect ranges for traces.
        output_add(tracker, DebugScriptOutputType_Trace, entity, now, scriptId, msg, range);
        break;
      }
    }
  }
}

static UiColor output_entry_bg_color(const DebugScriptOutput* entry) {
  switch (entry->type) {
  case DebugScriptOutputType_Trace:
    return ui_color(16, 64, 16, 192);
  case DebugScriptOutputType_Panic:
    return ui_color(64, 16, 16, 192);
  }
  diag_assert_fail("Invalid script output type");
  UNREACHABLE
}

static void output_options_draw(
    UiCanvasComp* canvas, DebugScriptPanelComp* panelComp, DebugScriptTrackerComp* tracker) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Mode:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->outputMode, g_outputModeNames, DebugScriptOutputMode_Count);

  ui_table_next_column(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Clear"))) {
    output_clear(tracker);
  }

  ui_layout_pop(canvas);
}

static void output_panel_tab_draw(
    UiCanvasComp*           canvas,
    DebugScriptPanelComp*   panelComp,
    DebugScriptTrackerComp* tracker,
    SceneSetEnvComp*        setEnv,
    EcsIterator*            subjectItr) {
  output_options_draw(canvas, panelComp, tracker);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 160);
  ui_table_add_column(&table, UiTableColumn_Fixed, 300);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Entity"), string_lit("Script entity.")},
          {string_lit("Message"), string_lit("Script output message.")},
          {string_lit("Location"), string_lit("Script output location.")},
      });

  const u32 numEntries = panelComp->lastRowCount;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numEntries));

  if (!numEntries) {
    ui_label(canvas, string_lit("No output entries."), .align = UiAlign_MiddleCenter);
  }

  panelComp->lastRowCount = 0;
  dynarray_for_t(&tracker->entries, DebugScriptOutput, entry) {
    if (panelComp->outputMode == DebugScriptOutputMode_Self) {
      if (!subjectItr || ecs_view_entity(subjectItr) != entry->entity) {
        continue;
      }
    }

    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, output_entry_bg_color(entry));

    ui_label_entity(canvas, entry->entity);
    ui_layout_push(canvas);
    ui_layout_inner(
        canvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(25, 25), UiBase_Absolute);
    const bool selected = scene_set_main(setEnv, g_sceneSetSelected) == entry->entity;
    if (ui_button(
            canvas,
            .label      = ui_shape_scratch(UiShape_SelectAll),
            .frameColor = selected ? ui_color(8, 128, 8, 192) : ui_color(32, 32, 32, 192),
            .fontSize   = 18,
            .tooltip    = g_tooltipSelectEntity)) {
      scene_set_clear(setEnv, g_sceneSetSelected);
      scene_set_add(setEnv, g_sceneSetSelected, entry->entity);
    }
    ui_layout_pop(canvas);

    ui_table_next_column(canvas, &table);
    ui_label(canvas, mem_create(entry->msgData, entry->msgLength), .selectable = true);

    const String locText = fmt_write_scratch(
        "{}:{}:{}-{}:{}",
        fmt_text(entry->scriptId),
        fmt_int(entry->range.start.line + 1),
        fmt_int(entry->range.start.column + 1),
        fmt_int(entry->range.end.line + 1),
        fmt_int(entry->range.end.column + 1));

    ui_table_next_column(canvas, &table);
    if (ui_button(canvas, .label = locText, .noFrame = true, .tooltip = g_tooltipOpenScript)) {
      panelComp->editorReq =
          (DebugEditorRequest){.scriptId = entry->scriptId, .pos = entry->range.start};
    }
    ++panelComp->lastRowCount;
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void script_panel_draw(
    EcsWorld*               world,
    UiCanvasComp*           canvas,
    DebugScriptPanelComp*   panelComp,
    DebugScriptTrackerComp* tracker,
    SceneSetEnvComp*        setEnv,
    EcsIterator*            assetItr,
    EcsIterator*            subjectItr) {
  const String title = fmt_write_scratch("{} Script Panel", fmt_ui_shape(Description));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title       = title,
      .tabNames    = g_scriptTabNames,
      .tabCount    = DebugScriptTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  switch (panelComp->panel.activeTab) {
  case DebugScriptTab_Info:
    if (subjectItr) {
      info_panel_tab_draw(world, canvas, panelComp, assetItr, subjectItr);
    } else {
      ui_label(canvas, string_lit("Select a scripted entity."), .align = UiAlign_MiddleCenter);
    }
    break;
  case DebugScriptTab_Memory:
    if (subjectItr) {
      memory_panel_tab_draw(canvas, panelComp, subjectItr);
    } else {
      ui_label(canvas, string_lit("Select a scripted entity."), .align = UiAlign_MiddleCenter);
    }
    break;
  case DebugScriptTab_Output:
    output_panel_tab_draw(canvas, panelComp, tracker, setEnv, subjectItr);
    break;
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_maybe_write(DebugScriptTrackerComp);
  ecs_access_read(AssetManagerComp);
  ecs_access_write(SceneSetEnvComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugScriptPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void debug_editor_update(DebugScriptPanelComp* panelComp, const AssetManagerComp* assets) {
  if (panelComp->editorLaunch && !process_poll(panelComp->editorLaunch)) {
    const ProcessExitCode exitCode = process_block(panelComp->editorLaunch);
    if (exitCode != 0) {
      log_e("Failed to start editor", log_param("code", fmt_int(exitCode)));
    }
    process_destroy(panelComp->editorLaunch);
    panelComp->editorLaunch = null;
  }

  if (!panelComp->editorLaunch && !string_is_empty(panelComp->editorReq.scriptId)) {
    DebugEditorRequest* req     = &panelComp->editorReq;
    DynString           pathStr = dynstring_create(g_alloc_scratch, usize_kibibyte);
    if (asset_path_by_id(assets, req->scriptId, &pathStr)) {
      const String path = dynstring_view(&pathStr);

#if defined(VOLO_WIN32)
      const String editorFile = string_lit("code-tunnel.exe");
#else
      const String editorFile = string_lit("code");
#endif
      const String editorArgs[] = {
          string_lit("--reuse-window"),
          string_lit("--goto"),
          fmt_write_scratch(
              "{}:{}:{}", fmt_text(path), fmt_int(req->pos.line + 1), fmt_int(req->pos.column + 1)),
      };
      Process* p = process_create(g_alloc_heap, editorFile, editorArgs, array_elems(editorArgs), 0);
      panelComp->editorLaunch = p;
    }
    dynstring_destroy(&pathStr);
    *req = (DebugEditorRequest){0};
  }
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

  SceneSetEnvComp*        setEnv       = ecs_view_write_t(globalItr, SceneSetEnvComp);
  const AssetManagerComp* assetManager = ecs_view_read_t(globalItr, AssetManagerComp);

  EcsView*     assetView = ecs_world_view_t(world, AssetView);
  EcsIterator* assetItr  = ecs_view_itr(assetView);

  const StringHash selectedSet = g_sceneSetSelected;

  EcsView*     subjectView = ecs_world_view_t(world, SubjectView);
  EcsIterator* subjectItr  = ecs_view_maybe_at(subjectView, scene_set_main(setEnv, selectedSet));

  output_query(tracker, assetItr, subjectView);

  if (tracker->autoOpenOnPanic && output_has_panic(tracker)) {
    EcsIterator* windowItr = ecs_view_first(ecs_world_view_t(world, WindowView));
    if (windowItr) {
      debug_script_output_panel_open(world, ecs_view_entity(windowItr));
      tracker->autoOpenOnPanic = false;
    }
  }

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugScriptPanelComp* panelComp = ecs_view_write_t(itr, DebugScriptPanelComp);
    UiCanvasComp*         canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    script_panel_draw(world, canvas, panelComp, tracker, setEnv, assetItr, subjectItr);

    debug_editor_update(panelComp, assetManager);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_script_module) {
  ecs_register_comp(DebugScriptTrackerComp, .destructor = ecs_destruct_script_tracker);
  ecs_register_comp(DebugScriptPanelComp, .destructor = ecs_destroy_script_panel);

  ecs_register_view(PanelUpdateGlobalView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(SubjectView);
  ecs_register_view(AssetView);
  ecs_register_view(WindowView);

  ecs_register_system(
      DebugScriptUpdatePanelSys,
      ecs_view_id(PanelUpdateGlobalView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView),
      ecs_view_id(AssetView),
      ecs_view_id(WindowView));
}

EcsEntityId debug_script_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugScriptPanelComp, .panel = ui_panel(.size = ui_vector(800, 500)));
  return panelEntity;
}

EcsEntityId debug_script_output_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_empty_t(world, panelEntity, DebugPanelComp);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugScriptPanelComp,
      .panel = ui_panel(.size = ui_vector(800, 500), .activeTab = DebugScriptTab_Output));
  return panelEntity;
}
