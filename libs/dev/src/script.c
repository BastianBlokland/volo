#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "core_math.h"
#include "core_process.h"
#include "core_stringtable.h"
#include "dev_panel.h"
#include "dev_script.h"
#include "dev_widget.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "input_manager.h"
#include "log_logger.h"
#include "scene_camera.h"
#include "scene_debug.h"
#include "scene_name.h"
#include "scene_prefab.h"
#include "scene_property.h"
#include "scene_register.h"
#include "scene_script.h"
#include "scene_set.h"
#include "script_mem.h"
#include "script_panic.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

#define output_max_age time_seconds(60)
#define output_max_message_size 64

ASSERT(output_max_message_size < u8_max, "Message length has to be storable in a 8 bits")

static const String g_tooltipOpenScript   = string_static("Open script in external editor.");
static const String g_tooltipSelectEntity = string_static("Select the entity.");

typedef enum {
  DebugScriptTab_Info,
  DebugScriptTab_Memory,
  DebugScriptTab_Output,
  DebugScriptTab_Global,

  DebugScriptTab_Count,
} DebugScriptTab;

static const String g_scriptTabNames[] = {
    string_static("Info"),
    string_static("\uE322 Memory"),
    string_static("Output"),
    string_static("Global"),
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
  SceneScriptSlot       slot;
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

typedef struct {
  String       id;
  EcsEntityId  entity;
  u32          totalEntities;
  u32          totalOperations;
  TimeDuration totalDuration;
} DebugScriptAsset;

ecs_comp_define(DevScriptTrackerComp) {
  DynArray outputEntries; // DebugScriptOutput[]
  DynArray assetEntries;  // DebugScriptAsset[]
  bool     freezeAssets;
  bool     autoOpenOnPanic;
};

ecs_comp_define(DevScriptPanelComp) {
  UiPanel               panel;
  bool                  outputOnly;
  bool                  hideNullMemory;
  DebugScriptOutputMode outputMode;
  UiScrollview          scrollview;
  u32                   lastRowCount;
  DebugEditorRequest    editorReq;
  Process*              editorLaunch;
};

static void ecs_destruct_script_tracker(void* data) {
  DevScriptTrackerComp* comp = data;
  dynarray_destroy(&comp->outputEntries);
  dynarray_destroy(&comp->assetEntries);
}

static void ecs_destroy_script_panel(void* data) {
  DevScriptPanelComp* comp = data;
  if (comp->editorLaunch) {
    process_destroy(comp->editorLaunch);
  }
}

ecs_view_define(SubjectView) {
  ecs_access_write(ScenePropertyComp);
  ecs_access_maybe_write(SceneScriptComp);
  ecs_access_maybe_read(SceneDebugComp);
  ecs_access_maybe_read(ScenePrefabInstanceComp);
}

ecs_view_define(EntityRefView) {
  ecs_access_maybe_read(AssetComp);
  ecs_access_maybe_read(SceneNameComp);
}

ecs_view_define(AssetView) { ecs_access_read(AssetComp); }
ecs_view_define(WindowView) { ecs_access_with(GapWindowComp); }

static bool debug_script_is_readonly(EcsIterator* subjectItr) {
  const ScenePrefabInstanceComp* prefabInst = ecs_view_read_t(subjectItr, ScenePrefabInstanceComp);
  return prefabInst && prefabInst->variant != ScenePrefabVariant_Normal;
}

static void info_panel_tab_script_draw(
    EcsWorld*             world,
    UiCanvasComp*         c,
    DevScriptPanelComp*   panelComp,
    UiTable*              table,
    EcsIterator*          assetItr,
    SceneScriptComp*      scriptInstance,
    const SceneScriptSlot slot) {
  const SceneScriptStats* stats             = scene_script_stats(scriptInstance, slot);
  const EcsEntityId       scriptAssetEntity = scene_script_asset(scriptInstance, slot);
  ecs_view_jump(assetItr, scriptAssetEntity);
  const AssetComp* scriptAsset       = ecs_view_read_t(assetItr, AssetComp);
  const bool       scriptAssetError  = ecs_world_has_t(world, scriptAssetEntity, AssetFailedComp);
  const bool       scriptAssetLoaded = ecs_world_has_t(world, scriptAssetEntity, AssetLoadedComp);
  const String     scriptId          = asset_id(scriptAsset);

  ui_canvas_id_block_next(c);

  ui_table_next_row(c, table);
  ui_table_draw_row_bg(c, table, ui_color(48, 48, 48, 192));
  const bool active = ui_section(c, .label = fmt_write_scratch("Script [{}]", fmt_int(slot)));
  ui_table_next_column(c, table);
  ui_label(c, fmt_write_scratch("{}", fmt_text(scriptId)), .selectable = true);

  ui_layout_push(c);
  ui_layout_inner(c, UiBase_Current, UiAlign_MiddleRight, ui_vector(25, 25), UiBase_Absolute);
  if (ui_button(c, .label = ui_shape_scratch(UiShape_OpenInNew), .tooltip = g_tooltipOpenScript)) {
    panelComp->editorReq = (DebugEditorRequest){.scriptId = scriptId};
  }
  ui_layout_pop(c);

  if (active) {
    ui_table_next_row(c, table);
    ui_label(c, string_lit("Status"));
    ui_table_next_column(c, table);
    if (scriptAssetError) {
      ui_style_push(c);
      ui_style_color(c, ui_color_red);
      ui_label(c, string_lit("Invalid script"));
      ui_style_pop(c);
    } else {
      String label;
      if (scene_script_flags(scriptInstance) & SceneScriptFlags_Enabled) {
        label = string_lit("Running");
      } else if (scriptAssetLoaded) {
        label = string_lit("Idle");
      } else {
        label = string_lit("Loading script");
      }
      ui_label(c, label);
    }

    ui_table_next_row(c, table);
    ui_label(c, string_lit("Operations"));
    ui_table_next_column(c, table);
    ui_label(c, fmt_write_scratch("{}", fmt_int(stats->executedOps)));

    ui_table_next_row(c, table);
    ui_label(c, string_lit("Duration"));
    ui_table_next_column(c, table);
    ui_label(c, fmt_write_scratch("{}", fmt_duration(stats->executedDur)));
  }

  ui_canvas_id_block_next(c); // End on a stable id.
}

static void info_panel_tab_draw(
    EcsWorld*           world,
    UiCanvasComp*       c,
    DevScriptPanelComp* panelComp,
    EcsIterator*        assetItr,
    EcsIterator*        subjectItr) {
  diag_assert(subjectItr);

  SceneScriptComp* scriptInstance = ecs_view_write_t(subjectItr, SceneScriptComp);
  if (!scriptInstance) {
    ui_label(c, string_lit("No script statistics available."), .align = UiAlign_MiddleCenter);
    return;
  }
  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  if (!debug_script_is_readonly(subjectItr)) {
    ui_table_next_row(c, &table);
    bool enabled = (scene_script_flags(scriptInstance) & SceneScriptFlags_Enabled) != 0;
    ui_label(c, string_lit("Enabled"));
    ui_table_next_column(c, &table);
    if (ui_toggle(c, &enabled)) {
      scene_script_flags_toggle(scriptInstance, SceneScriptFlags_Enabled);
    }
  }

  const u32 scriptCount = scene_script_count(scriptInstance);
  for (SceneScriptSlot slot = 0; slot != scriptCount; ++slot) {
    info_panel_tab_script_draw(world, c, panelComp, &table, assetItr, scriptInstance, slot);
  }
}

static bool memory_draw_bool(UiCanvasComp* c, ScriptVal* value) {
  bool valBool = script_get_bool(*value, false);
  if (ui_toggle(c, &valBool)) {
    *value = script_bool(valBool);
    return true;
  }
  return false;
}

static bool memory_draw_num(UiCanvasComp* c, ScriptVal* value) {
  f64 valNumber = script_get_num(*value, 0);
  if (ui_numbox(c, &valNumber, .min = f64_min, .max = f64_max)) {
    *value = script_num(valNumber);
    return true;
  }
  return false;
}

static bool memory_draw_vec3(UiCanvasComp* c, ScriptVal* value) {
  GeoVector vec3 = script_get_vec3(*value, geo_vector(0));
  if (debug_widget_vec3(c, &vec3, UiWidget_Default)) {
    *value = script_vec3(vec3);
    return true;
  }
  return false;
}

static bool memory_draw_quat(UiCanvasComp* c, ScriptVal* value) {
  GeoQuat quat = script_get_quat(*value, geo_quat_ident);
  if (debug_widget_quat(c, &quat, UiWidget_Default)) {
    *value = script_quat(quat);
    return true;
  }
  return false;
}

static bool memory_draw_color(UiCanvasComp* c, ScriptVal* value) {
  GeoColor col = script_get_color(*value, geo_color_clear);
  if (debug_widget_color(c, &col, UiWidget_Default)) {
    *value = script_color(col);
    return true;
  }
  return false;
}

static bool memory_draw_entity(UiCanvasComp* c, EcsIterator* entityRefItr, ScriptVal* value) {
  const EcsEntityId valEntity = script_get_entity(*value, ecs_entity_invalid);

  const u32 index  = ecs_entity_id_index(valEntity);
  const u32 serial = ecs_entity_id_serial(valEntity);

  String label = fmt_write_scratch("{}", ecs_entity_fmt(valEntity));
  if (ecs_view_maybe_jump(entityRefItr, valEntity)) {
    const AssetComp*     assetComp = ecs_view_read_t(entityRefItr, AssetComp);
    const SceneNameComp* nameComp  = ecs_view_read_t(entityRefItr, SceneNameComp);

    if (assetComp) {
      label = asset_id(assetComp);
    } else if (nameComp) {
      const String name = stringtable_lookup(g_stringtable, nameComp->name);
      label             = string_is_empty(name) ? string_lit("< Unnamed >") : name;
    }
  }

  const String tooltip = fmt_write_scratch(
      "Entity:\a>0C{}\n"
      "Index:\a>0C{}\n"
      "Serial:\a>0C{}\n",
      ecs_entity_fmt(valEntity),
      fmt_int(index),
      fmt_int(serial));

  ui_style_push(c);
  ui_style_variation(c, UiVariation_Monospace);
  ui_label(c, label, .selectable = true, .tooltip = tooltip);
  ui_style_pop(c);

  return false;
}

static bool memory_draw_str(UiCanvasComp* c, ScriptVal* value) {
  ui_label(c, script_val_scratch(*value));
  return false;
}

static bool memory_draw_val(UiCanvasComp* c, EcsIterator* entityRefItr, ScriptVal* value) {
  switch (script_type(*value)) {
  case ScriptType_Null:
    ui_label(c, string_lit("< Null >"));
    return false;
  case ScriptType_Num:
    return memory_draw_num(c, value);
  case ScriptType_Bool:
    return memory_draw_bool(c, value);
  case ScriptType_Vec3:
    return memory_draw_vec3(c, value);
  case ScriptType_Quat:
    return memory_draw_quat(c, value);
  case ScriptType_Color:
    return memory_draw_color(c, value);
  case ScriptType_Entity:
    return memory_draw_entity(c, entityRefItr, value);
  case ScriptType_Str:
    return memory_draw_str(c, value);
  case ScriptType_Count:
    break;
  }
  return false;
}

static void memory_options_draw(UiCanvasComp* c, DevScriptPanelComp* panelComp) {
  ui_layout_push(c);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 105);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);

  ui_table_next_row(c, &table);
  ui_label(c, string_lit("Hide null:"));
  ui_table_next_column(c, &table);
  ui_toggle(c, &panelComp->hideNullMemory);

  ui_layout_pop(c);
}

static i8 memory_compare_entry_name(const void* a, const void* b) {
  return compare_string(field_ptr(a, DebugMemoryEntry, name), field_ptr(b, DebugMemoryEntry, name));
}

static void memory_panel_tab_draw(
    UiCanvasComp*       c,
    DevScriptPanelComp* panelComp,
    EcsIterator*        entityRefItr,
    EcsIterator*        subject) {
  diag_assert(subject);

  ScenePropertyComp* propComp = ecs_view_write_t(subject, ScenePropertyComp);
  ScriptMem*         memory   = scene_prop_memory_mut(propComp);

  memory_options_draw(c, panelComp);
  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      c,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Key"), string_lit("Memory key.")},
          {string_lit("Type"), string_lit("Memory value type.")},
          {string_lit("Value"), string_lit("Memory value.")},
      });

  DynArray entries = dynarray_create_t(g_allocScratch, DebugMemoryEntry, 256);
  for (ScriptMemItr itr = script_mem_begin(memory); itr.key; itr = script_mem_next(memory, itr)) {
    if (panelComp->hideNullMemory && !script_non_null(script_mem_load(memory, itr.key))) {
      continue;
    }
    const String name = stringtable_lookup(g_stringtable, itr.key);

    *dynarray_push_t(&entries, DebugMemoryEntry) = (DebugMemoryEntry){
        .key  = itr.key,
        .name = string_is_empty(name) ? string_lit("< unnamed >") : name,
    };
  }

  dynarray_sort(&entries, memory_compare_entry_name);

  const f32 totalHeight = ui_table_height(&table, (u32)entries.size);
  ui_scrollview_begin(c, &panelComp->scrollview, UiLayer_Normal, totalHeight);

  if (entries.size) {
    dynarray_for_t(&entries, DebugMemoryEntry, entry) {
      ScriptVal value = script_mem_load(memory, entry->key);

      ui_table_next_row(c, &table);
      ui_table_draw_row_bg(c, &table, ui_color(48, 48, 48, 192));

      ui_label(c, entry->name, .selectable = true);
      ui_table_next_column(c, &table);

      ui_label(c, script_val_type_str(script_type(value)));
      ui_table_next_column(c, &table);

      if (memory_draw_val(c, entityRefItr, &value)) {
        script_mem_store(memory, entry->key, value);
      }
    }
  } else {
    ui_label(c, string_lit("Memory empty."), .align = UiAlign_MiddleCenter);
  }

  dynarray_destroy(&entries);

  ui_scrollview_end(c, &panelComp->scrollview);
  ui_layout_container_pop(c);
}

static DevScriptTrackerComp* tracker_create(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      DevScriptTrackerComp,
      .outputEntries   = dynarray_create_t(g_allocHeap, DebugScriptOutput, 64),
      .assetEntries    = dynarray_create_t(g_allocHeap, DebugScriptAsset, 32),
      .autoOpenOnPanic = true);
}

static i8 tracker_compare_asset(const void* a, const void* b) {
  return ecs_compare_entity(
      field_ptr(a, DebugScriptAsset, entity), field_ptr(b, DebugScriptAsset, entity));
}

static bool tracker_has_panic(const DevScriptTrackerComp* tracker) {
  dynarray_for_t(&tracker->outputEntries, DebugScriptOutput, entry) {
    if (entry->type == DebugScriptOutputType_Panic) {
      return true;
    }
  }
  return false;
}

static void tracker_output_clear(DevScriptTrackerComp* tracker) {
  dynarray_clear(&tracker->outputEntries);
}

static void tracker_prune_older(DevScriptTrackerComp* tracker, const TimeReal timestamp) {
  for (usize i = tracker->outputEntries.size; i-- != 0;) {
    if (dynarray_at_t(&tracker->outputEntries, i, DebugScriptOutput)->timestamp < timestamp) {
      dynarray_remove_unordered(&tracker->outputEntries, i, 1);
    }
  }
}

static void tracker_output_add(
    DevScriptTrackerComp*       tracker,
    const DebugScriptOutputType type,
    const EcsEntityId           entity,
    const TimeReal              time,
    const SceneScriptSlot       slot,
    const String                scriptId,
    const String                msg,
    const ScriptRangeLineCol    range) {
  DebugScriptOutput* entry = null;
  // Find an existing entry of the same type for the same entity.
  for (usize i = 0; i != tracker->outputEntries.size; ++i) {
    DebugScriptOutput* other = dynarray_at_t(&tracker->outputEntries, i, DebugScriptOutput);
    if (other->type == type && other->entity == entity && other->slot == slot) {
      entry = other;
      break;
    }
  }
  if (!entry) {
    // No existing entry found; add a new one.
    entry = dynarray_push_t(&tracker->outputEntries, DebugScriptOutput);
  }
  entry->type      = type;
  entry->slot      = slot;
  entry->msgLength = math_min((u8)msg.size, output_max_message_size);
  entry->timestamp = time;
  entry->entity    = entity;
  entry->scriptId  = scriptId;
  entry->range     = range;
  mem_cpy(mem_create(entry->msgData, entry->msgLength), string_slice(msg, 0, entry->msgLength));
}

static void tracker_asset_add(
    DevScriptTrackerComp*   tracker,
    const EcsEntityId       entity,
    const String            id,
    const SceneScriptStats* stats) {
  const DebugScriptAsset compareTarget = {.entity = entity};
  DebugScriptAsset*      entry =
      dynarray_find_or_insert_sorted(&tracker->assetEntries, tracker_compare_asset, &compareTarget);

  entry->id     = id;
  entry->entity = entity;
  entry->totalEntities += 1;
  entry->totalOperations += stats->executedOps;
  entry->totalDuration += stats->executedDur;
}

typedef enum {
  TrackerQueryFlags_QueryAssets = 1 << 0,
} TrackerQueryFlags;

static void tracker_query(
    DevScriptTrackerComp*   tracker,
    EcsIterator*            assetItr,
    EcsView*                subjectView,
    const TrackerQueryFlags flags) {
  const TimeReal now          = time_real_clock();
  const TimeReal oldestToKeep = time_real_offset(now, -output_max_age);
  tracker_prune_older(tracker, oldestToKeep);

  const AssetComp* assetComps[32];

  if (!tracker->freezeAssets) {
    dynarray_clear(&tracker->assetEntries);
  }

  for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
    const EcsEntityId      entity         = ecs_view_entity(itr);
    const SceneScriptComp* scriptInstance = ecs_view_read_t(itr, SceneScriptComp);
    const SceneDebugComp*  debug          = ecs_view_read_t(itr, SceneDebugComp);
    if (!scriptInstance) {
      continue;
    }
    const bool  didPanic   = (scene_script_flags(scriptInstance) & SceneScriptFlags_DidPanic) != 0;
    const usize debugCount = debug ? scene_debug_count(debug) : 0;
    if (!(flags & TrackerQueryFlags_QueryAssets) && !didPanic && !debugCount) {
      continue; // Early out when we don't need to query assets and there was no output.
    }

    const u32 scriptCount = scene_script_count(scriptInstance);
    for (SceneScriptSlot slot = 0; slot != scriptCount; ++slot) {
      diag_assert(slot < array_elems(assetComps));

      ecs_view_jump(assetItr, scene_script_asset(scriptInstance, slot));
      assetComps[slot] = ecs_view_read_t(assetItr, AssetComp);

      if ((flags & TrackerQueryFlags_QueryAssets) && !tracker->freezeAssets) {
        const SceneScriptStats* stats = scene_script_stats(scriptInstance, slot);
        tracker_asset_add(tracker, ecs_view_entity(assetItr), asset_id(assetComps[slot]), stats);
      }

      // Output panics.
      const ScriptPanic* panic = scene_script_panic(scriptInstance, slot);
      if (panic) {
        const String                scriptId = asset_id(assetComps[slot]);
        const String                msg  = script_panic_scratch(panic, ScriptPanicOutput_Default);
        const DebugScriptOutputType type = DebugScriptOutputType_Panic;
        tracker_output_add(tracker, type, entity, now, slot, scriptId, msg, panic->range);
      }
    }

    // Output traces.
    if (debug) {
      const SceneDebug* debugData = scene_debug_data(debug);
      for (usize i = 0; i != debugCount; ++i) {
        if (debugData[i].type == SceneDebugType_Trace) {
          const SceneScriptSlot       scriptSlot = debugData[i].src.scriptSlot;
          const String                scriptId   = asset_id(assetComps[scriptSlot]);
          const String                msg        = debugData[i].data_trace.text;
          const ScriptRangeLineCol    range      = debugData[i].src.scriptPos;
          const DebugScriptOutputType type       = DebugScriptOutputType_Trace;
          tracker_output_add(tracker, type, entity, now, scriptSlot, scriptId, msg, range);
        }
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

static void
output_options_draw(UiCanvasComp* c, DevScriptPanelComp* panelComp, DevScriptTrackerComp* tracker) {
  ui_layout_push(c);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);

  ui_table_next_row(c, &table);
  ui_label(c, string_lit("Mode:"));
  ui_table_next_column(c, &table);
  ui_select(c, (i32*)&panelComp->outputMode, g_outputModeNames, DebugScriptOutputMode_Count);

  ui_table_next_column(c, &table);
  if (ui_button(c, .label = string_lit("Clear"))) {
    tracker_output_clear(tracker);
  }

  ui_layout_pop(c);
}

static void output_panel_tab_draw(
    UiCanvasComp*         c,
    DevScriptPanelComp*   panelComp,
    DevScriptTrackerComp* tracker,
    SceneSetEnvComp*      setEnv,
    EcsIterator*          subjectItr) {
  output_options_draw(c, panelComp, tracker);
  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 215);
  ui_table_add_column(&table, UiTableColumn_Fixed, 300);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      c,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Entity"), string_lit("Script entity.")},
          {string_lit("Message"), string_lit("Script output message.")},
          {string_lit("Location"), string_lit("Script output location.")},
      });

  const u32 numEntries = panelComp->lastRowCount;
  const f32 height     = ui_table_height(&table, numEntries);
  ui_scrollview_begin(c, &panelComp->scrollview, UiLayer_Normal, height);

  if (!numEntries) {
    ui_label(c, string_lit("No output entries."), .align = UiAlign_MiddleCenter);
  }

  panelComp->lastRowCount = 0;
  dynarray_for_t(&tracker->outputEntries, DebugScriptOutput, entry) {
    switch (panelComp->outputMode) {
    case DebugScriptOutputMode_All:
      break;
    case DebugScriptOutputMode_Self:
      if (!subjectItr || ecs_view_entity(subjectItr) != entry->entity) {
        continue; // Entry does not belong to the subject.
      }
    case DebugScriptOutputMode_Count:
      break;
    }

    ui_table_next_row(c, &table);
    ui_table_draw_row_bg(c, &table, output_entry_bg_color(entry));

    ui_label_entity(c, entry->entity);
    ui_layout_push(c);
    ui_layout_inner(c, UiBase_Current, UiAlign_MiddleRight, ui_vector(25, 25), UiBase_Absolute);
    const bool selected = scene_set_contains(setEnv, g_sceneSetSelected, entry->entity);
    if (ui_button(
            c,
            .label      = ui_shape_scratch(UiShape_SelectAll),
            .frameColor = selected ? ui_color(8, 128, 8, 192) : ui_color(32, 32, 32, 192),
            .fontSize   = 18,
            .tooltip    = g_tooltipSelectEntity)) {
      scene_set_clear(setEnv, g_sceneSetSelected);
      scene_set_add(setEnv, g_sceneSetSelected, entry->entity, SceneSetFlags_None);
    }
    ui_layout_pop(c);

    ui_table_next_column(c, &table);
    ui_label(c, mem_create(entry->msgData, entry->msgLength), .selectable = true);

    const String locText = fmt_write_scratch(
        "{}:{}:{}-{}:{}",
        fmt_text(entry->scriptId),
        fmt_int(entry->range.start.line + 1),
        fmt_int(entry->range.start.column + 1),
        fmt_int(entry->range.end.line + 1),
        fmt_int(entry->range.end.column + 1));

    const String locTooltip = fmt_write_scratch(
        "{}\n\n\a.bScript\ar:\a>10{}\n\a.bLine\ar:\a>10{} - {}\n\a.bColumn\ar:\a>10{} - {}",
        fmt_text(g_tooltipOpenScript),
        fmt_text(entry->scriptId),
        fmt_int(entry->range.start.line + 1),
        fmt_int(entry->range.end.line + 1),
        fmt_int(entry->range.start.column + 1),
        fmt_int(entry->range.end.column + 1));

    ui_table_next_column(c, &table);
    if (ui_button(c, .label = locText, .noFrame = true, .tooltip = locTooltip)) {
      panelComp->editorReq =
          (DebugEditorRequest){.scriptId = entry->scriptId, .pos = entry->range.start};
    }
    ++panelComp->lastRowCount;
  }
  ui_canvas_id_block_next(c);

  ui_scrollview_end(c, &panelComp->scrollview);
  ui_layout_container_pop(c);
}

static void global_options_draw(UiCanvasComp* c, DevScriptTrackerComp* tracker) {
  ui_layout_push(c);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);

  ui_table_next_row(c, &table);
  ui_label(c, string_lit("Freeze:"));
  ui_table_next_column(c, &table);
  ui_toggle(c, &tracker->freezeAssets);

  ui_layout_pop(c);
}

static void global_panel_tab_draw(
    UiCanvasComp* c, DevScriptPanelComp* panelComp, DevScriptTrackerComp* tracker) {
  global_options_draw(c, tracker);
  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 350);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      c,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Script"), string_lit("Script asset.")},
          {string_lit("Actions"), string_lit("Actions to run for the scripts.")},
          {string_lit("Entities"), string_lit("Amount of entities that run the script.")},
          {string_lit("Operations"), string_lit("Total operations that the script runs.")},
          {string_lit("Time"), string_lit("Time execution time for the script.")},
      });

  const u32 numScripts = (u32)tracker->assetEntries.size;
  const f32 height     = ui_table_height(&table, numScripts);
  ui_scrollview_begin(c, &panelComp->scrollview, UiLayer_Normal, height);

  if (!numScripts) {
    ui_label(c, string_lit("No active scripts."), .align = UiAlign_MiddleCenter);
  }

  dynarray_for_t(&tracker->assetEntries, DebugScriptAsset, entry) {
    ui_table_next_row(c, &table);
    ui_table_draw_row_bg(c, &table, ui_color(48, 48, 48, 192));

    ui_label(c, entry->id, .selectable = true);
    ui_table_next_column(c, &table);
    ui_layout_resize(c, UiAlign_MiddleLeft, ui_vector(25, 0), UiBase_Absolute, Ui_X);
    if (ui_button(
            c, .label = ui_shape_scratch(UiShape_OpenInNew), .tooltip = g_tooltipOpenScript)) {
      panelComp->editorReq = (DebugEditorRequest){.scriptId = entry->id};
    }
    ui_table_next_column(c, &table);
    ui_label(c, fmt_write_scratch("{}", fmt_int(entry->totalEntities)));
    ui_table_next_column(c, &table);
    ui_label(c, fmt_write_scratch("{}", fmt_int(entry->totalOperations)));
    ui_table_next_column(c, &table);
    ui_label(c, fmt_write_scratch("{}", fmt_duration(entry->totalDuration)));
  }
  ui_canvas_id_block_next(c);

  ui_scrollview_end(c, &panelComp->scrollview);
  ui_layout_container_pop(c);
}

static void script_panel_draw(
    EcsWorld*             world,
    UiCanvasComp*         c,
    DevScriptPanelComp*   panelComp,
    DevScriptTrackerComp* tracker,
    SceneSetEnvComp*      setEnv,
    EcsIterator*          entityRefItr,
    EcsIterator*          assetItr,
    EcsIterator*          subjectItr) {
  const String title = fmt_write_scratch("{} Script Panel", fmt_ui_shape(Description));
  ui_panel_begin(
      c,
      &panelComp->panel,
      .title       = title,
      .tabNames    = g_scriptTabNames,
      .tabCount    = DebugScriptTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  switch (panelComp->panel.activeTab) {
  case DebugScriptTab_Info:
    if (subjectItr) {
      info_panel_tab_draw(world, c, panelComp, assetItr, subjectItr);
    } else {
      ui_label(c, string_lit("Select a scripted entity."), .align = UiAlign_MiddleCenter);
    }
    break;
  case DebugScriptTab_Memory:
    if (subjectItr) {
      memory_panel_tab_draw(c, panelComp, entityRefItr, subjectItr);
    } else {
      ui_label(c, string_lit("Select a scripted entity."), .align = UiAlign_MiddleCenter);
    }
    break;
  case DebugScriptTab_Output:
    output_panel_tab_draw(c, panelComp, tracker, setEnv, subjectItr);
    break;
  case DebugScriptTab_Global:
    global_panel_tab_draw(c, panelComp, tracker);
    break;
  }

  ui_panel_end(c, &panelComp->panel);
}

static void script_panel_draw_output_only(
    UiCanvasComp*         c,
    DevScriptPanelComp*   panelComp,
    DevScriptTrackerComp* tracker,
    SceneSetEnvComp*      setEnv,
    EcsIterator*          subjectItr) {
  const String title = fmt_write_scratch("{} Script Output", fmt_ui_shape(Description));
  ui_panel_begin(c, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  output_panel_tab_draw(c, panelComp, tracker, setEnv, subjectItr);

  ui_panel_end(c, &panelComp->panel);
}

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_maybe_write(DevScriptTrackerComp);
  ecs_access_read(AssetManagerComp);
  ecs_access_write(SceneSetEnvComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevScriptPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevScriptPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void debug_editor_update(DevScriptPanelComp* panelComp, const AssetManagerComp* assets) {
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
    DynString           pathStr = dynstring_create(g_allocScratch, usize_kibibyte);
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
      Process* p = process_create(g_allocHeap, editorFile, editorArgs, array_elems(editorArgs), 0);
      panelComp->editorLaunch = p;
    }
    dynstring_destroy(&pathStr);
    *req = (DebugEditorRequest){0};
  }
}

static bool dev_panel_needs_asset_query(EcsView* panelView) {
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DevScriptPanelComp* panelComp = ecs_view_write_t(itr, DevScriptPanelComp);
    const bool          pinned    = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue;
    }
    if (panelComp->panel.activeTab == DebugScriptTab_Global) {
      return true;
    }
  }
  return true;
}

ecs_system_define(DebugScriptUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DevScriptTrackerComp* tracker = ecs_view_write_t(globalItr, DevScriptTrackerComp);
  if (!tracker) {
    tracker = tracker_create(world);
  }

  SceneSetEnvComp*        setEnv       = ecs_view_write_t(globalItr, SceneSetEnvComp);
  const AssetManagerComp* assetManager = ecs_view_read_t(globalItr, AssetManagerComp);

  EcsIterator* entityRefItr = ecs_view_itr(ecs_world_view_t(world, EntityRefView));
  EcsIterator* assetItr     = ecs_view_itr(ecs_world_view_t(world, AssetView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);

  const StringHash selectedSet = g_sceneSetSelected;

  EcsView*     subjectView = ecs_world_view_t(world, SubjectView);
  EcsIterator* subjectItr  = ecs_view_maybe_at(subjectView, scene_set_main(setEnv, selectedSet));

  TrackerQueryFlags queryFlags = 0;
  if (dev_panel_needs_asset_query(panelView)) {
    queryFlags |= TrackerQueryFlags_QueryAssets;
  }
  tracker_query(tracker, assetItr, subjectView, queryFlags);

  if (tracker->autoOpenOnPanic && tracker_has_panic(tracker)) {
    EcsIterator* windowItr = ecs_view_first(ecs_world_view_t(world, WindowView));
    if (windowItr) {
      dev_script_panel_open_output(world, ecs_view_entity(windowItr));
      tracker->autoOpenOnPanic = false;
    }
  }

  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DevScriptPanelComp* panelComp = ecs_view_write_t(itr, DevScriptPanelComp);
    UiCanvasComp*       canvas    = ecs_view_write_t(itr, UiCanvasComp);

    debug_editor_update(panelComp, assetManager);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue;
    }
    if (panelComp->outputOnly) {
      script_panel_draw_output_only(canvas, panelComp, tracker, setEnv, subjectItr);
    } else {
      script_panel_draw(
          world, canvas, panelComp, tracker, setEnv, entityRefItr, assetItr, subjectItr);
    }

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_view_define(RayUpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_write(SceneDebugEnvComp);
}

ecs_view_define(RayUpdateWindowView) {
  ecs_access_with(GapWindowComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_system_define(DebugScriptUpdateRaySys) {
  EcsView*     globalView = ecs_world_view_t(world, RayUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneDebugEnvComp*      debugEnv = ecs_view_write_t(globalItr, SceneDebugEnvComp);
  const InputManagerComp* input    = ecs_view_read_t(globalItr, InputManagerComp);

  EcsView*     camView = ecs_world_view_t(world, RayUpdateWindowView);
  EcsIterator* camItr  = ecs_view_maybe_at(camView, input_active_window(input));
  if (!camItr) {
    return; // No active window.
  }

  const SceneCameraComp*    cam      = ecs_view_read_t(camItr, SceneCameraComp);
  const SceneTransformComp* camTrans = ecs_view_read_t(camItr, SceneTransformComp);

  const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
  const f32       inputAspect  = input_cursor_aspect(input);
  const GeoRay    inputRay     = scene_camera_ray(cam, camTrans, inputAspect, inputNormPos);

  scene_debug_ray_update(debugEnv, inputRay);
}

ecs_module_init(debug_script_module) {
  ecs_register_comp(DevScriptTrackerComp, .destructor = ecs_destruct_script_tracker);
  ecs_register_comp(DevScriptPanelComp, .destructor = ecs_destroy_script_panel);

  ecs_register_view(SubjectView);
  ecs_register_view(EntityRefView);
  ecs_register_view(AssetView);
  ecs_register_view(WindowView);

  ecs_register_system(
      DebugScriptUpdatePanelSys,
      ecs_register_view(PanelUpdateGlobalView),
      ecs_register_view(PanelUpdateView),
      ecs_view_id(SubjectView),
      ecs_view_id(EntityRefView),
      ecs_view_id(AssetView),
      ecs_view_id(WindowView));

  ecs_register_system(
      DebugScriptUpdateRaySys,
      ecs_register_view(RayUpdateGlobalView),
      ecs_register_view(RayUpdateWindowView));

  ecs_order(DebugScriptUpdateRaySys, SceneOrder_ScriptUpdate - 1);
}

EcsEntityId
dev_script_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId   panelEntity = dev_panel_create(world, window, type);
  DevScriptPanelComp* scriptPanel = ecs_world_add_t(
      world, panelEntity, DevScriptPanelComp, .panel = ui_panel(.size = ui_vector(800, 600)));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&scriptPanel->panel);
  }

  return panelEntity;
}

EcsEntityId dev_script_panel_open_output(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId   panelEntity = dev_panel_create(world, window, DevPanelType_Normal);
  DevScriptPanelComp* scriptPanel = ecs_world_add_t(
      world,
      panelEntity,
      DevScriptPanelComp,
      .panel      = ui_panel(.size = ui_vector(800, 600)),
      .outputOnly = true);

  ui_panel_pin(&scriptPanel->panel); // Output panel is always pinned.

  return panelEntity;
}
