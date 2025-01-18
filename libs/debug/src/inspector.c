#include "asset_manager.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "core_format.h"
#include "core_math.h"
#include "core_string.h"
#include "core_stringtable.h"
#include "core_utf8.h"
#include "debug_finder.h"
#include "debug_gizmo.h"
#include "debug_inspector.h"
#include "debug_panel.h"
#include "debug_prefab.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_stats.h"
#include "debug_text.h"
#include "debug_widget.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "geo_capsule.h"
#include "geo_query.h"
#include "geo_sphere.h"
#include "input_manager.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_attack.h"
#include "scene_bounds.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_debug.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_light.h"
#include "scene_location.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_nav.h"
#include "scene_prefab.h"
#include "scene_property.h"
#include "scene_renderable.h"
#include "scene_script.h"
#include "scene_set.h"
#include "scene_status.h"
#include "scene_tag.h"
#include "scene_target.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "scene_visibility.h"
#include "script_mem.h"
#include "trace_tracer.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

typedef enum {
  DebugInspectorSpace_Local,
  DebugInspectorSpace_World,

  DebugInspectorSpace_Count,
} DebugInspectorSpace;

typedef enum {
  DebugInspectorTool_None,
  DebugInspectorTool_Translation,
  DebugInspectorTool_Rotation,
  DebugInspectorTool_Scale,
  DebugInspectorTool_Picker,

  DebugInspectorTool_Count,
} DebugInspectorTool;

typedef enum {
  DebugInspectorVis_Icon,
  DebugInspectorVis_Explicit,
  DebugInspectorVis_Origin,
  DebugInspectorVis_Name,
  DebugInspectorVis_Locomotion,
  DebugInspectorVis_Collision,
  DebugInspectorVis_CollisionBounds,
  DebugInspectorVis_BoundsLocal,
  DebugInspectorVis_BoundsGlobal,
  DebugInspectorVis_NavigationPath,
  DebugInspectorVis_NavigationGrid,
  DebugInspectorVis_Light,
  DebugInspectorVis_Health,
  DebugInspectorVis_Attack,
  DebugInspectorVis_Target,
  DebugInspectorVis_Vision,
  DebugInspectorVis_Location,

  DebugInspectorVis_Count,
} DebugInspectorVis;

typedef enum {
  DebugInspectorVisMode_SelectedOnly,
  DebugInspectorVisMode_All,

  DebugInspectorVisMode_Count,
  DebugInspectorVisMode_Default = DebugInspectorVisMode_SelectedOnly,
} DebugInspectorVisMode;

typedef enum {
  DebugPropType_Num,
  DebugPropType_Bool,
  DebugPropType_Vec3,
  DebugPropType_Quat,
  DebugPropType_Color,
  DebugPropType_Str,
  DebugPropType_LevelEntity,
  DebugPropType_Decal,
  DebugPropType_Graphic,
  DebugPropType_Sound,
  DebugPropType_Vfx,

  DebugPropType_Count,
} DebugPropType;

typedef struct {
  String     name;
  StringHash key;
  ScriptVal  val;
} DebugPropEntry;

static const String g_spaceNames[] = {
    [DebugInspectorSpace_Local] = string_static("Local"),
    [DebugInspectorSpace_World] = string_static("World"),
};
ASSERT(array_elems(g_spaceNames) == DebugInspectorSpace_Count, "Missing space name");

static const String g_toolNames[] = {
    [DebugInspectorTool_None]        = string_static("None"),
    [DebugInspectorTool_Translation] = string_static("Translation"),
    [DebugInspectorTool_Rotation]    = string_static("Rotation"),
    [DebugInspectorTool_Scale]       = string_static("Scale"),
    [DebugInspectorTool_Picker]      = string_static("Picker"),
};
ASSERT(array_elems(g_toolNames) == DebugInspectorTool_Count, "Missing tool name");

static const String g_visNames[] = {
    [DebugInspectorVis_Icon]            = string_static("Icon"),
    [DebugInspectorVis_Explicit]        = string_static("Explicit"),
    [DebugInspectorVis_Origin]          = string_static("Origin"),
    [DebugInspectorVis_Name]            = string_static("Name"),
    [DebugInspectorVis_Locomotion]      = string_static("Locomotion"),
    [DebugInspectorVis_Collision]       = string_static("Collision"),
    [DebugInspectorVis_CollisionBounds] = string_static("CollisionBounds"),
    [DebugInspectorVis_BoundsLocal]     = string_static("BoundsLocal"),
    [DebugInspectorVis_BoundsGlobal]    = string_static("BoundsGlobal"),
    [DebugInspectorVis_NavigationPath]  = string_static("NavigationPath"),
    [DebugInspectorVis_NavigationGrid]  = string_static("NavigationGrid"),
    [DebugInspectorVis_Light]           = string_static("Light"),
    [DebugInspectorVis_Health]          = string_static("Health"),
    [DebugInspectorVis_Attack]          = string_static("Attack"),
    [DebugInspectorVis_Target]          = string_static("Target"),
    [DebugInspectorVis_Vision]          = string_static("Vision"),
    [DebugInspectorVis_Location]        = string_static("Location"),
};
ASSERT(array_elems(g_visNames) == DebugInspectorVis_Count, "Missing vis name");

static const String g_visModeNames[] = {
    [DebugInspectorVisMode_SelectedOnly] = string_static("SelectedOnly"),
    [DebugInspectorVisMode_All]          = string_static("All"),
};
ASSERT(array_elems(g_visModeNames) == DebugInspectorVisMode_Count, "Missing vis mode name");

static const String g_propTypeNames[] = {
    [DebugPropType_Num]         = string_static("Num"),
    [DebugPropType_Bool]        = string_static("Bool"),
    [DebugPropType_Vec3]        = string_static("Vec3"),
    [DebugPropType_Quat]        = string_static("Quat"),
    [DebugPropType_Color]       = string_static("Color"),
    [DebugPropType_Str]         = string_static("Str"),
    [DebugPropType_LevelEntity] = string_static("LevelEntity"),
    [DebugPropType_Decal]       = string_static("Decal"),
    [DebugPropType_Graphic]     = string_static("Graphic"),
    [DebugPropType_Sound]       = string_static("Sound"),
    [DebugPropType_Vfx]         = string_static("Vfx"),
};
ASSERT(array_elems(g_propTypeNames) == DebugPropType_Count, "Missing type name");

ecs_comp_define(DebugInspectorSettingsComp) {
  DebugInspectorSpace   space;
  DebugInspectorTool    tool;
  DebugInspectorVisMode visMode;
  SceneNavLayer         visNavLayer;
  u32                   visFlags;
  bool                  drawVisInGame;
  DebugInspectorTool    toolPickerPrevTool;
  EcsEntityId           toolPickerResult;
  GeoQuat               toolRotation; // Cached rotation to support world-space rotation tools.
};

ecs_comp_define(DebugInspectorPanelComp) {
  UiPanel       panel;
  UiScrollview  scrollview;
  u32           totalRows;
  DebugPropType newPropType;
  ScriptVal     newPropVal;
  DynString     newSetBuffer, newPropBuffer;
  GeoVector transformRotEulerDeg; // Local copy of rotation as euler angles to use while editing.
};

static void ecs_destruct_panel_comp(void* data) {
  DebugInspectorPanelComp* panel = data;
  dynstring_destroy(&panel->newSetBuffer);
  dynstring_destroy(&panel->newPropBuffer);
}

static i8 debug_prop_compare_entry(const void* a, const void* b) {
  return compare_string(field_ptr(a, DebugPropEntry, name), field_ptr(b, DebugPropEntry, name));
}

ecs_view_define(SettingsWriteView) { ecs_access_write(DebugInspectorSettingsComp); }

ecs_view_define(GlobalPanelUpdateView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_write(DebugFinderComp);
  ecs_access_write(DebugStatsGlobalComp);
  ecs_access_write(ScenePrefabEnvComp);
  ecs_access_write(SceneSetEnvComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugInspectorPanelComp's are exclusively managed here.

  ecs_access_read(DebugPanelComp);
  ecs_access_write(DebugInspectorPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(GlobalToolUpdateView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(DebugGizmoComp);
  ecs_access_write(DebugInspectorSettingsComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugStatsGlobalComp);
  ecs_access_write(DebugTextComp);
  ecs_access_write(InputManagerComp);
  ecs_access_write(SceneSetEnvComp);
}

ecs_view_define(GlobalVisDrawView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneSetEnvComp);
  ecs_access_write(DebugInspectorSettingsComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugStatsGlobalComp);
  ecs_access_write(DebugTextComp);
}

ecs_view_define(SubjectView) {
  ecs_access_maybe_read(SceneAttackTraceComp);
  ecs_access_maybe_read(SceneDebugComp);
  ecs_access_maybe_read(SceneLifetimeDurationComp);
  ecs_access_maybe_read(SceneLifetimeOwnerComp);
  ecs_access_maybe_read(SceneLocomotionComp);
  ecs_access_maybe_read(SceneNameComp);
  ecs_access_maybe_read(SceneNavAgentComp);
  ecs_access_maybe_read(SceneNavPathComp);
  ecs_access_maybe_read(SceneScriptComp);
  ecs_access_maybe_read(SceneSetMemberComp);
  ecs_access_maybe_read(SceneStatusComp);
  ecs_access_maybe_read(SceneTargetTraceComp);
  ecs_access_maybe_read(SceneVelocityComp);
  ecs_access_maybe_read(SceneVisionComp);
  ecs_access_maybe_write(SceneAttachmentComp);
  ecs_access_maybe_write(SceneAttackComp);
  ecs_access_maybe_write(SceneBoundsComp);
  ecs_access_maybe_write(SceneCollisionComp);
  ecs_access_maybe_write(SceneFactionComp);
  ecs_access_maybe_write(SceneHealthComp);
  ecs_access_maybe_write(SceneLightAmbientComp);
  ecs_access_maybe_write(SceneLightDirComp);
  ecs_access_maybe_write(SceneLightPointComp);
  ecs_access_maybe_write(SceneLocationComp);
  ecs_access_maybe_write(ScenePrefabInstanceComp);
  ecs_access_maybe_write(ScenePropertyComp);
  ecs_access_maybe_write(SceneRenderableComp);
  ecs_access_maybe_write(SceneScaleComp);
  ecs_access_maybe_write(SceneTagComp);
  ecs_access_maybe_write(SceneTargetFinderComp);
  ecs_access_maybe_write(SceneVfxDecalComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(TransformView) { ecs_access_read(SceneTransformComp); }

ecs_view_define(ScriptAssetView) {
  ecs_access_with(AssetLoadedComp);
  ecs_access_read(AssetScriptComp);
}

ecs_view_define(EntityRefView) {
  ecs_access_maybe_read(AssetComp);
  ecs_access_maybe_read(SceneBoundsComp);
  ecs_access_maybe_read(SceneNameComp);
  ecs_access_maybe_read(ScenePrefabInstanceComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_view_define(CameraView) {
  ecs_access_read(GapWindowAspectComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

ecs_view_define(PrefabMapView) { ecs_access_read(AssetPrefabMapComp); }

static void inspector_notify_vis(
    DebugInspectorSettingsComp* set, DebugStatsGlobalComp* stats, const DebugInspectorVis vis) {
  debug_stats_notify(
      stats,
      fmt_write_scratch("Visualize {}", fmt_text(g_visNames[vis])),
      (set->visFlags & (1 << vis)) ? string_lit("enabled") : string_lit("disabled"));
}

static void
inspector_notify_vis_mode(DebugStatsGlobalComp* stats, const DebugInspectorVisMode visMode) {
  debug_stats_notify(stats, string_lit("Visualize"), g_visModeNames[visMode]);
}

static bool inspector_is_edit_variant(EcsIterator* subject) {
  if (!subject) {
    return false;
  }
  const ScenePrefabInstanceComp* prefabInstComp = ecs_view_read_t(subject, ScenePrefabInstanceComp);
  return prefabInstComp && prefabInstComp->variant == ScenePrefabVariant_Edit;
}

static void inspector_extract_props(const ScenePropertyComp* comp, ScenePrefabSpec* out) {
  enum { MaxResults = 128 };

  ScenePrefabProperty* res      = alloc_array_t(g_allocScratch, ScenePrefabProperty, MaxResults);
  u16                  resCount = 0;

  const ScriptMem* memory = scene_prop_memory(comp);
  for (ScriptMemItr itr = script_mem_begin(memory); itr.key; itr = script_mem_next(memory, itr)) {
    const ScriptVal val = script_mem_load(memory, itr.key);
    if (script_type(val) != ScriptType_Null) {
      if (resCount == MaxResults) {
        break; // Maximum properties reached. TODO: Should this be an error?
      }
      res[resCount++] = (ScenePrefabProperty){.key = itr.key, .value = val};
    }
  }

  out->properties    = res;
  out->propertyCount = resCount;
}

static void inspector_extract_sets(const SceneSetMemberComp* comp, ScenePrefabSpec* out) {
  ASSERT(array_elems(out->sets) >= scene_set_member_max_sets, "Insufficient set storage");
  scene_set_member_all(comp, out->sets);
}

static EcsEntityId inspector_prefab_duplicate(EcsWorld* world, EcsIterator* subject) {
  const EcsEntityId              entity         = ecs_view_entity(subject);
  const SceneTransformComp*      transComp      = ecs_view_read_t(subject, SceneTransformComp);
  const SceneScaleComp*          scaleComp      = ecs_view_read_t(subject, SceneScaleComp);
  const SceneFactionComp*        factionComp    = ecs_view_read_t(subject, SceneFactionComp);
  const ScenePrefabInstanceComp* prefabInstComp = ecs_view_read_t(subject, ScenePrefabInstanceComp);
  if (UNLIKELY(!prefabInstComp || prefabInstComp->variant == ScenePrefabVariant_Preview)) {
    log_e("Unable to duplicate prefab.", log_param("entity", ecs_entity_fmt(entity)));
    return ecs_entity_invalid;
  }
  ScenePrefabSpec spec = {
      .id       = 0 /* Entity will get a new id on level save */,
      .prefabId = prefabInstComp->prefabId,
      .variant  = prefabInstComp->variant,
      .faction  = factionComp ? factionComp->id : SceneFaction_None,
      .scale    = scaleComp ? scaleComp->scale : 1.0f,
      .position = transComp->position,
      .rotation = transComp->rotation,
  };
  const ScenePropertyComp* propComp = ecs_view_read_t(subject, ScenePropertyComp);
  if (propComp && prefabInstComp->variant == ScenePrefabVariant_Edit) {
    /**
     * Preserve properties for edit variants, runtime variants shouldn't preserve properties as it
     * could lead to inconsistent script state.
     */
    inspector_extract_props(propComp, &spec);
  }
  const SceneSetMemberComp* setMember = ecs_view_read_t(subject, SceneSetMemberComp);
  if (setMember) {
    inspector_extract_sets(setMember, &spec);
  }
  return scene_prefab_spawn(world, &spec);
}

static void inspector_prefab_replace(
    ScenePrefabEnvComp* prefabEnv, EcsIterator* subject, const StringHash prefabId) {
  const EcsEntityId              entity         = ecs_view_entity(subject);
  const SceneTransformComp*      transComp      = ecs_view_read_t(subject, SceneTransformComp);
  const SceneScaleComp*          scaleComp      = ecs_view_read_t(subject, SceneScaleComp);
  const SceneFactionComp*        factionComp    = ecs_view_read_t(subject, SceneFactionComp);
  const ScenePrefabInstanceComp* prefabInstComp = ecs_view_read_t(subject, ScenePrefabInstanceComp);
  if (UNLIKELY(!prefabInstComp || prefabInstComp->variant != ScenePrefabVariant_Edit)) {
    // NOTE: Play-variant instances cannot be replaced due to incompatible trait data.
    log_e("Unable to replace prefab.", log_param("entity", ecs_entity_fmt(entity)));
    return;
  }
  ScenePrefabSpec spec = {
      .id       = prefabInstComp->id,
      .prefabId = prefabId,
      .variant  = ScenePrefabVariant_Edit,
      .faction  = factionComp ? factionComp->id : SceneFaction_None,
      .scale    = scaleComp ? scaleComp->scale : 1.0f,
      .position = transComp->position,
      .rotation = transComp->rotation,
  };
  const ScenePropertyComp* propComp = ecs_view_read_t(subject, ScenePropertyComp);
  if (propComp) {
    inspector_extract_props(propComp, &spec);
  }
  const SceneSetMemberComp* setMember = ecs_view_read_t(subject, SceneSetMemberComp);
  if (setMember) {
    inspector_extract_sets(setMember, &spec);
  }
  scene_prefab_spawn_replace(prefabEnv, &spec, entity);
}

static void inspector_prop_find_inputs(
    EcsIterator* subject, EcsIterator* scriptAssetItr, DynArray* outInputKeys /* String[] */) {
  const SceneScriptComp* scriptComp = ecs_view_read_t(subject, SceneScriptComp);
  if (!scriptComp) {
    return;
  }
  const u32 scriptCount = scene_script_count(scriptComp);
  for (SceneScriptSlot scriptSlot = 0; scriptSlot != scriptCount; ++scriptSlot) {
    if (!ecs_view_maybe_jump(scriptAssetItr, scene_script_asset(scriptComp, scriptSlot))) {
      continue; // Script is not loaded yet or failed to load.
    }
    const AssetScriptComp* scriptAsset = ecs_view_read_t(scriptAssetItr, AssetScriptComp);
    heap_array_for_t(scriptAsset->inputKeys, StringHash, key) {
      const String name = stringtable_lookup(g_stringtable, *key);
      if (LIKELY(!string_is_empty(name))) {
        *(String*)dynarray_find_or_insert_sorted(outInputKeys, compare_string, &name) = name;
      }
    }
  }
}

static void
inspector_prop_collect(EcsIterator* subject, DynArray* outEntries /* DebugPropEntry[] */) {
  const ScenePropertyComp* propComp = ecs_view_read_t(subject, ScenePropertyComp);
  if (!propComp) {
    return;
  }
  const ScriptMem* memory = scene_prop_memory(propComp);
  for (ScriptMemItr itr = script_mem_begin(memory); itr.key; itr = script_mem_next(memory, itr)) {
    const ScriptVal val = script_mem_load(memory, itr.key);
    if (script_type(val) != ScriptType_Null) {
      const String keyStr                          = stringtable_lookup(g_stringtable, itr.key);
      *dynarray_push_t(outEntries, DebugPropEntry) = (DebugPropEntry){
          .name = string_is_empty(keyStr) ? string_lit("< unnamed >") : keyStr,
          .key  = itr.key,
          .val  = val,
      };
    }
  }
  dynarray_sort(outEntries, debug_prop_compare_entry);
}

typedef struct {
  EcsWorld*                   world;
  UiCanvasComp*               canvas;
  DebugInspectorPanelComp*    panel;
  const SceneTimeComp*        time;
  ScenePrefabEnvComp*         prefabEnv;
  const AssetPrefabMapComp*   prefabMap;
  SceneSetEnvComp*            setEnv;
  DebugStatsGlobalComp*       stats;
  DebugInspectorSettingsComp* settings;
  DebugFinderComp*            finder;
  EcsIterator*                scriptAssetItr;
  EcsIterator*                entityRefItr;
  EcsIterator*                subject;
  EcsEntityId                 subjectEntity;
  bool                        isEditMode;
} InspectorContext;

static bool inspector_panel_section(InspectorContext* ctx, String title, const bool readonly) {
  String tooltip = string_empty;
  if (readonly) {
    title   = fmt_write_scratch("{} \uE897", fmt_text(title));
    tooltip = string_lit("Readonly section.");
  }
  bool open;
  ui_layout_push(ctx->canvas);
  {
    ui_layout_move_to(ctx->canvas, UiBase_Container, UiAlign_MiddleLeft, Ui_X);
    ui_layout_resize_to(ctx->canvas, UiBase_Container, UiAlign_MiddleRight, Ui_X);

    ui_style_push(ctx->canvas);
    {
      ui_style_color(ctx->canvas, ui_color(0, 0, 0, 128));
      ui_style_outline(ctx->canvas, 2);
      ui_canvas_draw_glyph(ctx->canvas, UiShape_Square, 10, UiFlags_None);
    }
    ui_style_pop(ctx->canvas);

    ui_layout_grow(ctx->canvas, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);
    open = ui_section(ctx->canvas, .label = title, .tooltip = tooltip);
  }
  ui_layout_pop(ctx->canvas);
  return open;
}

static void inspector_panel_next(InspectorContext* ctx, UiTable* table) {
  ui_table_next_row(ctx->canvas, table);
  ++ctx->panel->totalRows;
}

static void inspector_panel_draw_string(InspectorContext* ctx, const String value) {
  ui_style_push(ctx->canvas);
  ui_style_variation(ctx->canvas, UiVariation_Monospace);
  ui_label(ctx->canvas, value, .selectable = true);
  ui_style_pop(ctx->canvas);
}

static void inspector_panel_draw_entity(InspectorContext* ctx, const EcsEntityId value) {
  DynString tooltipBuffer = dynstring_create(g_allocScratch, usize_kibibyte);

  String label      = fmt_write_scratch("{}", ecs_entity_fmt(value));
  bool   selectable = false, monospace = true;
  if (!ecs_entity_valid(value)) {
    label     = string_lit("< None >");
    monospace = false;
  } else if (ecs_view_maybe_jump(ctx->entityRefItr, value)) {
    const AssetComp*     assetComp = ecs_view_read_t(ctx->entityRefItr, AssetComp);
    const SceneNameComp* nameComp  = ecs_view_read_t(ctx->entityRefItr, SceneNameComp);
    if (assetComp) {
      label = asset_id(assetComp);
      fmt_write(&tooltipBuffer, "Asset:\a>0C{}\n", fmt_text(label));
    } else if (nameComp) {
      const String name = stringtable_lookup(g_stringtable, nameComp->name);
      label             = string_is_empty(name) ? string_lit("< Unnamed >") : name;
      selectable        = true;
      fmt_write(&tooltipBuffer, "Name:\a>0C{}\n", fmt_text(label));
    }
  }

  fmt_write(
      &tooltipBuffer,
      "Entity:\a>0C{}\n"
      "Index:\a>0C{}\n"
      "Serial:\a>0C{}\n",
      ecs_entity_fmt(value),
      fmt_int(ecs_entity_id_index(value)),
      fmt_int(ecs_entity_id_serial(value)));

  ui_layout_push(ctx->canvas);
  ui_style_push(ctx->canvas);
  ui_style_variation(ctx->canvas, monospace ? UiVariation_Monospace : UiVariation_Normal);
  if (selectable) {
    ui_layout_grow(ctx->canvas, UiAlign_BottomLeft, ui_vector(-35, 0), UiBase_Absolute, Ui_X);
  }
  ui_label(ctx->canvas, label, .selectable = true, .tooltip = dynstring_view(&tooltipBuffer));
  if (selectable) {
    ui_layout_next(ctx->canvas, Ui_Right, 10);
    ui_layout_resize(ctx->canvas, UiAlign_BottomLeft, ui_vector(25, 22), UiBase_Absolute, Ui_XY);
    if (ui_button(
            ctx->canvas,
            .label      = ui_shape_scratch(UiShape_SelectAll),
            .fontSize   = 18,
            .frameColor = ui_color(0, 16, 255, 192),
            .tooltip    = string_lit("Select entity."))) {
      scene_set_clear(ctx->setEnv, g_sceneSetSelected);
      scene_set_add(ctx->setEnv, g_sceneSetSelected, value, SceneSetFlags_None);
    }
  }
  ui_style_pop(ctx->canvas);
  ui_layout_pop(ctx->canvas);
}

static void inspector_panel_draw_none(InspectorContext* ctx) {
  ui_style_push(ctx->canvas);
  ui_style_color_mult(ctx->canvas, 0.75f);
  inspector_panel_draw_string(ctx, string_lit("< None >"));
  ui_style_pop(ctx->canvas);
}

static void inspector_panel_draw_general(InspectorContext* ctx, UiTable* table) {
  inspector_panel_next(ctx, table);
  ui_label(ctx->canvas, string_lit("Entity identifier"));
  ui_table_next_column(ctx->canvas, table);
  if (ctx->subject) {
    ui_style_push(ctx->canvas);
    ui_style_variation(ctx->canvas, UiVariation_Monospace);
    ui_label_entity(ctx->canvas, ctx->subjectEntity);
    ui_style_pop(ctx->canvas);
  } else {
    inspector_panel_draw_none(ctx);
  }

  inspector_panel_next(ctx, table);
  ui_label(ctx->canvas, string_lit("Entity name"));
  ui_table_next_column(ctx->canvas, table);
  if (ctx->subject) {
    const SceneNameComp* nameComp = ecs_view_read_t(ctx->subject, SceneNameComp);
    if (nameComp) {
      const String name = stringtable_lookup(g_stringtable, nameComp->name);
      inspector_panel_draw_string(ctx, name);
    }
  } else {
    inspector_panel_draw_none(ctx);
  }

  inspector_panel_next(ctx, table);
  ui_label(ctx->canvas, string_lit("Entity prefab"));
  ui_table_next_column(ctx->canvas, table);
  ScenePrefabInstanceComp* prefabInst = null;
  if (ctx->subject) {
    prefabInst = ecs_view_write_t(ctx->subject, ScenePrefabInstanceComp);
  }
  if (prefabInst) {
    UiWidgetFlags flags = UiWidget_Default;
    if (prefabInst->variant != ScenePrefabVariant_Edit) {
      flags |= UiWidget_Disabled;
    }
    if (debug_widget_prefab(ctx->canvas, ctx->prefabMap, &prefabInst->prefabId, flags)) {
      inspector_prefab_replace(ctx->prefabEnv, ctx->subject, prefabInst->prefabId);
    }
  } else {
    inspector_panel_draw_none(ctx);
  }

  inspector_panel_next(ctx, table);
  ui_label(ctx->canvas, string_lit("Entity faction"));
  ui_table_next_column(ctx->canvas, table);
  SceneFactionComp* factionComp = null;
  if (ctx->subject) {
    factionComp = ecs_view_write_t(ctx->subject, SceneFactionComp);
  }
  if (factionComp) {
    debug_widget_faction(ctx->canvas, &factionComp->id, UiWidget_Default);
  } else {
    inspector_panel_draw_none(ctx);
  }
}

static void inspector_panel_draw_transform(InspectorContext* ctx, UiTable* table) {
  SceneTransformComp* transform = ecs_view_write_t(ctx->subject, SceneTransformComp);
  SceneScaleComp*     scale     = ecs_view_write_t(ctx->subject, SceneScaleComp);
  if (!transform && !scale) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (!inspector_panel_section(ctx, string_lit("Transform"), false /* readonly */)) {
    return;
  }
  if (transform) {
    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Position"));
    ui_table_next_column(ctx->canvas, table);
    if (debug_widget_vec3_resettable(ctx->canvas, &transform->position, UiWidget_Default)) {
      // Clamp the position to a sane value.
      transform->position = geo_vector_clamp(transform->position, 1e3f);
    }

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Rotation (Euler degrees)"));
    ui_table_next_column(ctx->canvas, table);
    if (debug_widget_vec3_resettable(
            ctx->canvas, &ctx->panel->transformRotEulerDeg, UiWidget_DirtyWhileEditing)) {
      const GeoVector eulerRad = geo_vector_mul(ctx->panel->transformRotEulerDeg, math_deg_to_rad);
      transform->rotation      = geo_quat_from_euler(eulerRad);
    } else {
      const GeoVector eulerRad         = geo_quat_to_euler(transform->rotation);
      ctx->panel->transformRotEulerDeg = geo_vector_mul(eulerRad, math_rad_to_deg);
    }
  }
  if (scale) {
    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Scale"));
    ui_table_next_column(ctx->canvas, table);
    if (debug_widget_f32(ctx->canvas, &scale->scale, UiWidget_Default)) {
      // Clamp the scale to a sane value.
      scale->scale = math_clamp_f32(scale->scale, 1e-2f, 1e2f);
    }
  }
}

static ScriptVal inspector_panel_prop_default(const DebugPropType type) {
  switch (type) {
  case DebugPropType_Num:
    return script_num(0);
  case DebugPropType_Bool:
    return script_bool(false);
  case DebugPropType_Vec3:
    return script_vec3_lit(0, 0, 0);
  case DebugPropType_Quat:
    return script_quat(geo_quat_ident);
  case DebugPropType_Color:
    return script_color(geo_color_white);
  case DebugPropType_Str:
    return script_str_empty();
  default:
    return script_null();
  }
}

static bool inspector_panel_prop_edit(InspectorContext* ctx, ScriptVal* val) {
  switch (script_type(*val)) {
  case ScriptType_Num: {
    f64 valNum = script_get_num(*val, 0);
    if (ui_numbox(ctx->canvas, &valNum, .min = f64_min, .max = f64_max)) {
      *val = script_num(valNum);
      return true;
    }
    return false;
  }
  case ScriptType_Bool: {
    bool valBool = script_get_bool(*val, false);
    if (ui_toggle(ctx->canvas, &valBool)) {
      *val = script_bool(valBool);
      return true;
    }
    return false;
  }
  case ScriptType_Vec3: {
    GeoVector valVec3 = script_get_vec3(*val, geo_vector(0));
    if (debug_widget_vec3(ctx->canvas, &valVec3, UiWidget_Default)) {
      *val = script_vec3(valVec3);
      return true;
    }
    return false;
  }
  case ScriptType_Quat: {
    GeoQuat valQuat = script_get_quat(*val, geo_quat_ident);
    if (debug_widget_quat(ctx->canvas, &valQuat, UiWidget_Default)) {
      *val = script_quat(valQuat);
      return true;
    }
    return false;
  }
  case ScriptType_Color: {
    GeoColor valColor = script_get_color(*val, geo_color_white);
    if (debug_widget_color(ctx->canvas, &valColor, UiWidget_Default)) {
      *val = script_color(valColor);
      return true;
    }
    return false;
  }
  case ScriptType_Str: {
    const String valStr = stringtable_lookup(g_stringtable, script_get_str(*val, 0));

    u8        editBuffer[64];
    DynString editStr = dynstring_create_over(mem_var(editBuffer));
    dynstring_append(&editStr, string_slice(valStr, 0, math_min(valStr.size, sizeof(editBuffer))));

    if (ui_textbox(ctx->canvas, &editStr, .maxTextLength = sizeof(editBuffer))) {
      // TODO: This hashes on every character typed which unnecessary fills the string-table.
      const StringHash newStrHash = stringtable_add(g_stringtable, dynstring_view(&editStr));
      *val                        = script_str(newStrHash);
      return true;
    }
    return false;
  }
  case ScriptType_Entity: {
    const EcsEntityId entity = script_get_entity(*val, 0);
    inspector_panel_draw_entity(ctx, entity);
    return false;
  }
  case ScriptType_Null:
    ui_label(ctx->canvas, string_lit("< Null >"));
    return false;
  case ScriptType_Count:
    break;
  }
  UNREACHABLE;
}

static bool inspector_panel_prop_edit_level_entity(InspectorContext* ctx, ScriptVal* val) {
  EcsEntityId entity     = script_get_entity(*val, 0);
  String      entityName = string_lit("< None >");
  if (ecs_view_maybe_jump(ctx->entityRefItr, entity)) {
    const SceneNameComp* nameComp = ecs_view_read_t(ctx->entityRefItr, SceneNameComp);
    if (nameComp) {
      entityName = stringtable_lookup(g_stringtable, nameComp->name);
      if (string_is_empty(entityName)) {
        entityName = string_lit("< Unnamed >");
      }
    }
  }
  bool changed = false;
  if (ctx->settings->tool == DebugInspectorTool_Picker) {
    if (ui_button(ctx->canvas, .label = string_lit("Cancel picking"))) {
      ctx->settings->tool = ctx->settings->toolPickerPrevTool;
      debug_stats_notify(ctx->stats, string_lit("Tool"), g_toolNames[ctx->settings->tool]);
    }
    if (entity != ctx->settings->toolPickerResult) {
      *val    = script_entity_or_null(ctx->settings->toolPickerResult);
      changed = true;
    }
  } else {
    if (ui_button(ctx->canvas, .label = fmt_write_scratch("Pick ({})", fmt_text(entityName)))) {
      ctx->settings->toolPickerPrevTool = ctx->settings->tool;
      ctx->settings->tool               = DebugInspectorTool_Picker;
      debug_stats_notify(ctx->stats, string_lit("Tool"), g_toolNames[DebugInspectorTool_Picker]);
    }
  }
  return changed;
}

static bool inspector_panel_prop_edit_asset(
    InspectorContext* ctx, ScriptVal* val, const DebugFinderCategory assetCat) {
  EcsEntityId entity = script_get_entity(*val, 0);
  if (debug_widget_asset(ctx->canvas, ctx->finder, assetCat, &entity, UiWidget_Default)) {
    *val = script_entity_or_null(entity);
    return true;
  }
  return false;
}

static String inspector_panel_prop_tooltip_scratch(const DebugPropEntry* entry) {
  return fmt_write_scratch(
      "Key name:\a>15{}\n"
      "Key hash:\a>15{}\n"
      "Type:\a>15{}\n"
      "Value:\a>15{}\n",
      fmt_text(entry->name),
      fmt_int(entry->key),
      fmt_text(script_val_type_str(script_type(entry->val))),
      fmt_text(script_val_scratch(entry->val)));
}

static void inspector_panel_prop_labels(UiCanvasComp* canvas, const String* inputEntry) {
  if (inputEntry) {
    ui_layout_push(canvas);
    ui_layout_next(canvas, Ui_Right, 0);
    ui_layout_resize(canvas, UiAlign_BottomRight, ui_vector(20, 20), UiBase_Absolute, Ui_XY);
    ui_style_push(canvas);
    ui_style_color(canvas, ui_color(255, 255, 255, 128));
    const UiId id = ui_canvas_draw_glyph(canvas, UiShape_Input, 0, UiFlags_Interactable);
    ui_tooltip(canvas, id, string_lit("This property is used as a script input."));
    ui_style_pop(canvas);
    ui_layout_pop(canvas);
  } else {
    ui_canvas_id_skip(canvas, 3 /* 1 for the glyph and 2 for the tooltip*/);
  }
}

static void inspector_panel_draw_properties(InspectorContext* ctx, UiTable* table) {
  ScenePropertyComp* propComp = ecs_view_write_t(ctx->subject, ScenePropertyComp);
  if (!propComp) {
    return;
  }
  ScriptMem* memory = scene_prop_memory_mut(propComp);

  inspector_panel_next(ctx, table);
  if (!inspector_panel_section(ctx, string_lit("Properties"), false /* readonly */)) {
    return;
  }
  DynArray entries = dynarray_create_t(g_allocScratch, DebugPropEntry, 128);
  inspector_prop_collect(ctx->subject, &entries);

  DynArray inputKeys = dynarray_create_t(g_allocScratch, String, 128);
  inspector_prop_find_inputs(ctx->subject, ctx->scriptAssetItr, &inputKeys);

  dynarray_for_t(&entries, DebugPropEntry, entry) {
    inspector_panel_next(ctx, table);

    const String tooltip = inspector_panel_prop_tooltip_scratch(entry);
    ui_label(ctx->canvas, entry->name, .selectable = true, .tooltip = tooltip);

    String* inputEntry = dynarray_search_binary(&inputKeys, compare_string, &entry->name);
    if (inputEntry) {
      dynarray_remove_ptr(&inputKeys, inputEntry); // Remove the used inputs from the preset list.
    }
    inspector_panel_prop_labels(ctx->canvas, inputEntry);

    ui_table_next_column(ctx->canvas, table);
    ui_layout_grow(ctx->canvas, UiAlign_BottomLeft, ui_vector(-35, 0), UiBase_Absolute, Ui_X);
    if (inspector_panel_prop_edit(ctx, &entry->val)) {
      script_mem_store(memory, entry->key, entry->val);
    }
    ui_layout_next(ctx->canvas, Ui_Right, 10);
    ui_layout_resize(ctx->canvas, UiAlign_BottomLeft, ui_vector(25, 22), UiBase_Absolute, Ui_XY);
    if (ui_button(
            ctx->canvas,
            .label      = ui_shape_scratch(UiShape_Delete),
            .fontSize   = 18,
            .frameColor = ui_color(255, 16, 0, 192),
            .tooltip    = string_lit("Remove this property entry."))) {
      script_mem_store(memory, entry->key, script_null());
    }
  }
  dynarray_destroy(&entries);

  // Entry creation Ui.
  inspector_panel_next(ctx, table);
  ui_textbox(
      ctx->canvas,
      &ctx->panel->newPropBuffer,
      .placeholder   = string_lit("New key..."),
      .tooltip       = string_lit("Key for a new property entry."),
      .type          = UiTextbox_Word,
      .maxTextLength = 32);
  ui_table_next_column(ctx->canvas, table);
  ui_layout_grow(ctx->canvas, UiAlign_BottomLeft, ui_vector(-35, 0), UiBase_Absolute, Ui_X);
  if (ui_select(
          ctx->canvas,
          (i32*)&ctx->panel->newPropType,
          g_propTypeNames,
          array_elems(g_propTypeNames))) {
    ctx->panel->newPropVal = inspector_panel_prop_default(ctx->panel->newPropType);
  }
  ui_layout_next(ctx->canvas, Ui_Right, 10);
  ui_layout_resize(ctx->canvas, UiAlign_BottomLeft, ui_vector(25, 22), UiBase_Absolute, Ui_XY);
  const bool valid = ctx->panel->newPropBuffer.size != 0 && script_non_null(ctx->panel->newPropVal);
  if (ui_button(
          ctx->canvas,
          .flags      = valid ? 0 : UiWidget_Disabled,
          .label      = ui_shape_scratch(UiShape_Add),
          .fontSize   = 18,
          .frameColor = ui_color(16, 192, 0, 192),
          .tooltip    = string_lit("Add a new property entry with the given key and type."))) {
    const String     keyName = dynstring_view(&ctx->panel->newPropBuffer);
    const StringHash key     = stringtable_add(g_stringtable, keyName);
    script_mem_store(memory, key, ctx->panel->newPropVal);
    dynstring_clear(&ctx->panel->newPropBuffer);
    ctx->panel->newPropVal = inspector_panel_prop_default(ctx->panel->newPropType);
  }
  inspector_panel_next(ctx, table);
  i32 preset = -1;
  if (ui_select(
          ctx->canvas,
          &preset,
          dynarray_begin_t(&inputKeys, String),
          (u32)inputKeys.size,
          .placeholder = string_lit("< Preset >"),
          .tooltip     = string_lit("Pick a key name from the script inputs."))) {
    dynstring_clear(&ctx->panel->newPropBuffer);
    dynstring_append(&ctx->panel->newPropBuffer, *dynarray_at_t(&inputKeys, preset, String));
  }
  ui_table_next_column(ctx->canvas, table);
  ui_layout_grow(ctx->canvas, UiAlign_BottomLeft, ui_vector(-35, 0), UiBase_Absolute, Ui_X);
  switch (ctx->panel->newPropType) {
  case DebugPropType_LevelEntity:
    inspector_panel_prop_edit_level_entity(ctx, &ctx->panel->newPropVal);
    break;
  case DebugPropType_Decal:
    inspector_panel_prop_edit_asset(ctx, &ctx->panel->newPropVal, DebugFinder_Decal);
    break;
  case DebugPropType_Graphic:
    inspector_panel_prop_edit_asset(ctx, &ctx->panel->newPropVal, DebugFinder_Graphic);
    break;
  case DebugPropType_Sound:
    inspector_panel_prop_edit_asset(ctx, &ctx->panel->newPropVal, DebugFinder_Sound);
    break;
  case DebugPropType_Vfx:
    inspector_panel_prop_edit_asset(ctx, &ctx->panel->newPropVal, DebugFinder_Vfx);
    break;
  default:
    inspector_panel_prop_edit(ctx, &ctx->panel->newPropVal);
    break;
  }
}

static void inspector_panel_draw_sets(InspectorContext* ctx, UiTable* table) {
  const SceneSetMemberComp* setMember = ecs_view_read_t(ctx->subject, SceneSetMemberComp);

  StringHash   sets[scene_set_member_max_sets];
  const u32    setCount    = setMember ? scene_set_member_all(setMember, sets) : 0;
  const u32    setCountMax = scene_set_member_max_sets;
  const String title = fmt_write_scratch("Sets ({} / {})", fmt_int(setCount), fmt_int(setCountMax));

  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, title, false /* readonly */)) {
    for (u32 i = 0; i != setCount; ++i) {
      inspector_panel_next(ctx, table);
      const String setName = stringtable_lookup(g_stringtable, sets[i]);
      ui_label(
          ctx->canvas,
          string_is_empty(setName) ? string_lit("< unknown >") : setName,
          .selectable = true,
          .tooltip    = fmt_write_scratch("Hash: {}", fmt_int(sets[i])));
      ui_table_next_column(ctx->canvas, table);
      ui_layout_inner(
          ctx->canvas, UiBase_Current, UiAlign_MiddleLeft, ui_vector(25, 22), UiBase_Absolute);
      if (ui_button(
              ctx->canvas,
              .label      = ui_shape_scratch(UiShape_Delete),
              .fontSize   = 18,
              .frameColor = ui_color(255, 16, 0, 192),
              .tooltip    = string_lit("Remove this entity from the set."))) {
        scene_set_remove(ctx->setEnv, sets[i], ctx->subjectEntity);
      }
    }

    if (setCount != setCountMax) {
      inspector_panel_next(ctx, table);
      ui_textbox(
          ctx->canvas,
          &ctx->panel->newSetBuffer,
          .placeholder   = string_lit("New set..."),
          .type          = UiTextbox_Word,
          .maxTextLength = 32);
      ui_table_next_column(ctx->canvas, table);
      ui_layout_inner(
          ctx->canvas, UiBase_Current, UiAlign_MiddleLeft, ui_vector(25, 22), UiBase_Absolute);
      if (ui_button(
              ctx->canvas,
              .flags      = ctx->panel->newSetBuffer.size == 0 ? UiWidget_Disabled : 0,
              .label      = ui_shape_scratch(UiShape_Add),
              .fontSize   = 18,
              .frameColor = ui_color(16, 192, 0, 192),
              .tooltip    = string_lit("Add this entity to the specified set."))) {
        const String     setName = dynstring_view(&ctx->panel->newSetBuffer);
        const StringHash set     = stringtable_add(g_stringtable, setName);
        scene_set_add(ctx->setEnv, set, ctx->subjectEntity, SceneSetFlags_None);
        dynstring_clear(&ctx->panel->newSetBuffer);
      }
    }
  }
}

static void inspector_panel_draw_renderable(InspectorContext* ctx, UiTable* table) {
  SceneRenderableComp* renderable = ecs_view_write_t(ctx->subject, SceneRenderableComp);
  if (!renderable) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Renderable"), ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Graphic"));
    ui_table_next_column(ctx->canvas, table);
    inspector_panel_draw_entity(ctx, renderable->graphic);

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Color"));
    ui_table_next_column(ctx->canvas, table);
    debug_widget_color(ctx->canvas, &renderable->color, flags);

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Emissive"));
    ui_table_next_column(ctx->canvas, table);
    ui_slider(ctx->canvas, &renderable->emissive, .flags = flags);
  }
}

static void inspector_panel_draw_lifetime(InspectorContext* ctx, UiTable* table) {
  const SceneLifetimeOwnerComp*    owner = ecs_view_read_t(ctx->subject, SceneLifetimeOwnerComp);
  const SceneLifetimeDurationComp* dur   = ecs_view_read_t(ctx->subject, SceneLifetimeDurationComp);
  if (!owner && !dur) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Lifetime"), ctx->isEditMode /* readonly */)) {
    if (owner) {
      for (u32 i = 0; i != array_elems(owner->owners); ++i) {
        inspector_panel_next(ctx, table);
        ui_label(ctx->canvas, fmt_write_scratch("Owner {}", fmt_int(i)));
        ui_table_next_column(ctx->canvas, table);
        inspector_panel_draw_entity(ctx, owner->owners[i]);
      }
    }
    if (dur) {
      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, string_lit("Time remaining"));
      ui_table_next_column(ctx->canvas, table);
      ui_label(ctx->canvas, fmt_write_scratch("{}", fmt_duration(dur->duration)));
    }
  }
}

static void inspector_panel_draw_attachment(InspectorContext* ctx, UiTable* table) {
  SceneAttachmentComp* attach = ecs_view_write_t(ctx->subject, SceneAttachmentComp);
  if (!attach) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Attachment"), ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Target"));
    ui_table_next_column(ctx->canvas, table);
    inspector_panel_draw_entity(ctx, attach->target);

    DynString jointName = dynstring_create(g_allocScratch, 64);
    if (attach->jointName) {
      dynstring_append(&jointName, stringtable_lookup(g_stringtable, attach->jointName));
    }

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Joint"));
    ui_table_next_column(ctx->canvas, table);
    if (ui_textbox(
            ctx->canvas, &jointName, .maxTextLength = 64, .type = UiTextbox_Word, .flags = flags)) {
      attach->jointIndex = sentinel_u32;
      attach->jointName  = string_maybe_hash(dynstring_view(&jointName));
    }

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Offset"));
    ui_table_next_column(ctx->canvas, table);
    debug_widget_vec3(ctx->canvas, &attach->offset, flags);
  }
}

static void inspector_panel_draw_script(InspectorContext* ctx, UiTable* table) {
  const SceneScriptComp* script = ecs_view_read_t(ctx->subject, SceneScriptComp);
  if (!script) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Script"), ctx->isEditMode /* readonly */)) {
    u32 scriptCount = scene_script_count(script);
    for (SceneScriptSlot slot = 0; slot != scriptCount; ++slot) {
      const EcsEntityId asset = scene_script_asset(script, slot);
      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, fmt_write_scratch("Asset {}", fmt_int(slot)));
      ui_table_next_column(ctx->canvas, table);
      inspector_panel_draw_entity(ctx, asset);
    }
  }
}

static void inspector_panel_draw_light(InspectorContext* ctx, UiTable* table) {
  SceneLightPointComp*   point = ecs_view_write_t(ctx->subject, SceneLightPointComp);
  SceneLightDirComp*     dir   = ecs_view_write_t(ctx->subject, SceneLightDirComp);
  SceneLightAmbientComp* amb   = ecs_view_write_t(ctx->subject, SceneLightAmbientComp);
  if (!point && !dir && !amb) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Light"), ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;
    if (point) {
      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, string_lit("Radiance"));
      ui_table_next_column(ctx->canvas, table);
      debug_widget_color(ctx->canvas, &point->radiance, flags);

      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, string_lit("Radius"));
      ui_table_next_column(ctx->canvas, table);
      if (debug_widget_f32(ctx->canvas, &point->radius, flags)) {
        // Clamp the radius to a sane value.
        point->radius = math_clamp_f32(point->radius, 1e-3f, 1e3f);
      }
    }
    if (dir) {
      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, string_lit("Radiance"));
      ui_table_next_column(ctx->canvas, table);
      debug_widget_color(ctx->canvas, &dir->radiance, flags);

      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, string_lit("Shadows"));
      ui_table_next_column(ctx->canvas, table);
      ui_toggle(ctx->canvas, &dir->shadows, .flags = flags);

      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, string_lit("Coverage"));
      ui_table_next_column(ctx->canvas, table);
      ui_toggle(ctx->canvas, &dir->coverage, .flags = flags);
    }
    if (amb) {
      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, string_lit("Ambient"));
      ui_table_next_column(ctx->canvas, table);
      if (debug_widget_f32(ctx->canvas, &amb->intensity, flags)) {
        // Clamp the ambient intensity to a sane value.
        amb->intensity = math_clamp_f32(amb->intensity, 0.0f, 10.0f);
      }
    }
  }
}

static void inspector_panel_draw_health(InspectorContext* ctx, UiTable* table) {
  SceneHealthComp* health = ecs_view_write_t(ctx->subject, SceneHealthComp);
  if (!health) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Health"), ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Amount"));
    ui_table_next_column(ctx->canvas, table);
    ui_slider(ctx->canvas, &health->norm, .flags = flags);

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Max"));
    ui_table_next_column(ctx->canvas, table);
    debug_widget_f32(ctx->canvas, &health->max, flags);
  }
}

static void inspector_panel_draw_status(InspectorContext* ctx, UiTable* table) {
  const SceneStatusComp* status = ecs_view_read_t(ctx->subject, SceneStatusComp);
  if (!status) {
    return;
  }
  inspector_panel_next(ctx, table);
  const u32    activeCount = bits_popcnt((u32)status->active);
  const String title       = fmt_write_scratch("Status ({})", fmt_int(activeCount));
  if (inspector_panel_section(ctx, title, ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;
    for (SceneStatusType type = 0; type != SceneStatusType_Count; ++type) {
      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, scene_status_name(type));
      ui_table_next_column(ctx->canvas, table);
      bool active = scene_status_active(status, type);
      if (ui_toggle(ctx->canvas, &active, .flags = flags)) {
        if (active) {
          const EcsEntityId instigator = 0;
          scene_status_add(ctx->world, ctx->subjectEntity, type, instigator);
        } else {
          scene_status_remove(ctx->world, ctx->subjectEntity, type);
        }
      }
    }
  }
}

static void inspector_panel_draw_target(InspectorContext* ctx, UiTable* table) {
  const SceneTargetFinderComp* finder = ecs_view_read_t(ctx->subject, SceneTargetFinderComp);
  if (!finder) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Target"), ctx->isEditMode /* readonly */)) {
    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Entity"));
    ui_table_next_column(ctx->canvas, table);
    inspector_panel_draw_entity(ctx, scene_target_primary(finder));

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Time until refresh"));
    ui_table_next_column(ctx->canvas, table);
    const TimeDuration untilRefresh = finder->nextRefreshTime - ctx->time->time;
    ui_label(ctx->canvas, fmt_write_scratch("{}", fmt_duration(untilRefresh)));
  }
}

static void inspector_panel_draw_nav_agent(InspectorContext* ctx, UiTable* table) {
  const SceneNavAgentComp* agent = ecs_view_read_t(ctx->subject, SceneNavAgentComp);
  if (!agent) {
    return;
  }
  inspector_panel_next(ctx, table);
  const String title = string_lit("Navigation Agent");
  if (inspector_panel_section(ctx, title, ctx->isEditMode /* readonly */)) {
    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Layer"));
    ui_table_next_column(ctx->canvas, table);
    ui_select(
        ctx->canvas,
        (i32*)&agent->layer,
        g_sceneNavLayerNames,
        SceneNavLayer_Count,
        .flags = UiWidget_Disabled);
  }
}

static void inspector_panel_draw_decal(InspectorContext* ctx, UiTable* table) {
  SceneVfxDecalComp* decal = ecs_view_write_t(ctx->subject, SceneVfxDecalComp);
  if (!decal) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Decal"), ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;
    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Alpha"));
    ui_table_next_column(ctx->canvas, table);
    ui_slider(ctx->canvas, &decal->alpha, .flags = flags);
  }
}

static void inspector_panel_draw_collision(InspectorContext* ctx, UiTable* table) {
  SceneCollisionComp* col = ecs_view_write_t(ctx->subject, SceneCollisionComp);
  if (!col) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Collision"), ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;
    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Layer"));
    ui_table_next_column(ctx->canvas, table);
    if (bits_popcnt((u32)col->layer) == 1) {
      inspector_panel_draw_string(ctx, scene_layer_name(col->layer));
    } else {
      inspector_panel_draw_string(ctx, string_lit("< Multiple >"));
    }

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Shapes"));
    ui_table_next_column(ctx->canvas, table);
    inspector_panel_draw_string(ctx, fmt_write_scratch("{}", fmt_int(col->shapeCount)));

    for (u32 i = 0; i != col->shapeCount; ++i) {
      SceneCollisionShape* shape = &col->shapes[i];

      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, fmt_write_scratch("[{}]\tType", fmt_int(i)));
      ui_table_next_column(ctx->canvas, table);
      inspector_panel_draw_string(ctx, scene_collision_type_name(shape->type));

      switch (shape->type) {
      case SceneCollisionType_Sphere: {
        inspector_panel_next(ctx, table);
        ui_label(ctx->canvas, string_lit("\tOffset"));
        ui_table_next_column(ctx->canvas, table);
        debug_widget_vec3(ctx->canvas, &shape->sphere.point, flags);

        inspector_panel_next(ctx, table);
        ui_label(ctx->canvas, string_lit("\tRadius"));
        ui_table_next_column(ctx->canvas, table);
        debug_widget_f32(ctx->canvas, &shape->sphere.radius, flags);
      } break;
      case SceneCollisionType_Capsule: {
        inspector_panel_next(ctx, table);
        ui_label(ctx->canvas, string_lit("\tA"));
        ui_table_next_column(ctx->canvas, table);
        debug_widget_vec3(ctx->canvas, &shape->capsule.line.a, flags);

        inspector_panel_next(ctx, table);
        ui_label(ctx->canvas, string_lit("\tB"));
        ui_table_next_column(ctx->canvas, table);
        debug_widget_vec3(ctx->canvas, &shape->capsule.line.b, flags);

        inspector_panel_next(ctx, table);
        ui_label(ctx->canvas, string_lit("\tRadius"));
        ui_table_next_column(ctx->canvas, table);
        debug_widget_f32(ctx->canvas, &shape->capsule.radius, flags);
      } break;
      case SceneCollisionType_Box: {
        inspector_panel_next(ctx, table);
        ui_label(ctx->canvas, string_lit("\tMin"));
        ui_table_next_column(ctx->canvas, table);
        debug_widget_vec3(ctx->canvas, &shape->box.box.min, flags);

        inspector_panel_next(ctx, table);
        ui_label(ctx->canvas, string_lit("\tMax"));
        ui_table_next_column(ctx->canvas, table);
        debug_widget_vec3(ctx->canvas, &shape->box.box.max, flags);
      } break;
      case SceneCollisionType_Count:
        UNREACHABLE
      }
    }
  }
}

static void inspector_panel_draw_location(InspectorContext* ctx, UiTable* table) {
  SceneLocationComp* location = ecs_view_write_t(ctx->subject, SceneLocationComp);
  if (!location) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Location"), ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;
    for (SceneLocationType type = 0; type != SceneLocationType_Count; ++type) {
      const String typeName = scene_location_type_name(type);

      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, fmt_write_scratch("{} Min", fmt_text(typeName)));
      ui_table_next_column(ctx->canvas, table);
      debug_widget_vec3(ctx->canvas, &location->volumes[type].min, flags);

      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, fmt_write_scratch("{} Max", fmt_text(typeName)));
      ui_table_next_column(ctx->canvas, table);
      debug_widget_vec3(ctx->canvas, &location->volumes[type].max, flags);
    }
  }
}

static void inspector_panel_draw_bounds(InspectorContext* ctx, UiTable* table) {
  SceneBoundsComp* boundsComp = ecs_view_write_t(ctx->subject, SceneBoundsComp);
  if (!boundsComp) {
    return;
  }
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Bounds"), ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags  = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;
    GeoVector           center = geo_box_center(&boundsComp->local);
    GeoVector           size   = geo_box_size(&boundsComp->local);
    bool                dirty  = false;

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Center"));
    ui_table_next_column(ctx->canvas, table);
    dirty |= debug_widget_vec3(ctx->canvas, &center, flags);

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Size"));
    ui_table_next_column(ctx->canvas, table);
    dirty |= debug_widget_vec3(ctx->canvas, &size, flags);

    if (dirty) {
      boundsComp->local = geo_box_from_center(center, size);
    }
  }
}

static void inspector_panel_draw_archetype(InspectorContext* ctx, UiTable* table) {
  const EcsArchetypeId archetype = ecs_world_entity_archetype(ctx->world, ctx->subjectEntity);
  const BitSet         compMask  = ecs_world_component_mask(ctx->world, archetype);
  const String         title     = fmt_write_scratch("Archetype (id: {})", fmt_int(archetype));

  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, title, ctx->isEditMode /* readonly */)) {
    const EcsDef* def = ecs_world_def(ctx->world);
    bitset_for(compMask, compId) {
      const String compName = ecs_def_comp_name(def, (EcsCompId)compId);
      const usize  compSize = ecs_def_comp_size(def, (EcsCompId)compId);
      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, compName);
      ui_table_next_column(ctx->canvas, table);
      inspector_panel_draw_string(
          ctx, fmt_write_scratch("id: {<3} size: {}", fmt_int(compId), fmt_size(compSize)));
    }
  }
}

static void inspector_panel_draw_tags(InspectorContext* ctx, UiTable* table) {
  SceneTagComp* tagComp = ecs_view_write_t(ctx->subject, SceneTagComp);
  if (!tagComp) {
    return;
  }
  const u32    tagCount = bits_popcnt((u32)tagComp->tags);
  const String title    = fmt_write_scratch("Tags ({})", fmt_int(tagCount));
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, title, ctx->isEditMode /* readonly */)) {
    const UiWidgetFlags flags = ctx->isEditMode ? UiWidget_Disabled : UiWidget_Default;
    for (u32 i = 0; i != SceneTags_Count; ++i) {
      const SceneTags tag = 1 << i;
      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, scene_tag_name(tag));
      ui_table_next_column(ctx->canvas, table);
      ui_toggle_flag(ctx->canvas, (u32*)&tagComp->tags, tag, .flags = flags);
    }
  }
}

static void inspector_panel_draw_settings(InspectorContext* ctx, UiTable* table) {
  inspector_panel_next(ctx, table);
  if (inspector_panel_section(ctx, string_lit("Settings"), false /* readonly */)) {
    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Space"));
    ui_table_next_column(ctx->canvas, table);
    if (ui_select(
            ctx->canvas, (i32*)&ctx->settings->space, g_spaceNames, array_elems(g_spaceNames))) {
      debug_stats_notify(ctx->stats, string_lit("Space"), g_spaceNames[ctx->settings->space]);
    }

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Tool"));
    ui_table_next_column(ctx->canvas, table);
    if (ui_select(ctx->canvas, (i32*)&ctx->settings->tool, g_toolNames, array_elems(g_toolNames))) {
      debug_stats_notify(ctx->stats, string_lit("Tool"), g_toolNames[ctx->settings->tool]);
    }

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Visualize In Game"));
    ui_table_next_column(ctx->canvas, table);
    ui_toggle(ctx->canvas, &ctx->settings->drawVisInGame);

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Navigation Layer"));
    ui_table_next_column(ctx->canvas, table);
    const String* layerNames = g_sceneNavLayerNames;
    if (ui_select(
            ctx->canvas, (i32*)&ctx->settings->visNavLayer, layerNames, SceneNavLayer_Count)) {
      debug_stats_notify(
          ctx->stats, string_lit("Navigation Layer"), layerNames[ctx->settings->visNavLayer]);
    }

    inspector_panel_next(ctx, table);
    ui_label(ctx->canvas, string_lit("Visualize Mode"));
    ui_table_next_column(ctx->canvas, table);
    ui_select(
        ctx->canvas, (i32*)&ctx->settings->visMode, g_visModeNames, array_elems(g_visModeNames));

    for (DebugInspectorVis vis = 0; vis != DebugInspectorVis_Count; ++vis) {
      inspector_panel_next(ctx, table);
      ui_label(ctx->canvas, fmt_write_scratch("Visualize {}", fmt_text(g_visNames[vis])));
      ui_table_next_column(ctx->canvas, table);
      if (ui_toggle_flag(ctx->canvas, (u32*)&ctx->settings->visFlags, 1 << vis)) {
        inspector_notify_vis(ctx->settings, ctx->stats, vis);
      }
    }
  }
}

static void inspector_panel_draw(InspectorContext* ctx) {
  const String title = fmt_write_scratch("{} Inspector Panel", fmt_ui_shape(ViewInAr));
  ui_panel_begin(
      ctx->canvas, &ctx->panel->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 215);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const f32 totalHeight = ui_table_height(&table, ctx->panel->totalRows);
  ui_scrollview_begin(ctx->canvas, &ctx->panel->scrollview, UiLayer_Normal, totalHeight);
  ctx->panel->totalRows = 0;

  /**
   * NOTE: The sections draw a variable amount of elements, thus we jump to the next id block
   * afterwards to keep consistent ids.
   */

  inspector_panel_draw_general(ctx, &table);
  ui_canvas_id_block_next(ctx->canvas);

  if (ctx->subject) {
    inspector_panel_draw_transform(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_properties(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_sets(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_renderable(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_lifetime(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_attachment(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_script(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_light(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_health(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_status(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_target(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_nav_agent(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_decal(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_collision(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_location(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_bounds(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_archetype(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);

    inspector_panel_draw_tags(ctx, &table);
    ui_canvas_id_block_next(ctx->canvas);
  }
  ui_canvas_id_block_next(ctx->canvas);

  inspector_panel_draw_settings(ctx, &table);
  ui_canvas_id_block_next(ctx->canvas);

  ui_scrollview_end(ctx->canvas, &ctx->panel->scrollview);
  ui_panel_end(ctx->canvas, &ctx->panel->panel);
}

static DebugInspectorSettingsComp* inspector_settings_get_or_create(EcsWorld* w) {
  const EcsEntityId global = ecs_world_global(w);
  EcsView*          view   = ecs_world_view_t(w, SettingsWriteView);
  EcsIterator*      itr    = ecs_view_maybe_at(view, global);
  if (itr) {
    return ecs_view_write_t(itr, DebugInspectorSettingsComp);
  }
  u32 defaultVisFlags = 0;
  defaultVisFlags |= 1 << DebugInspectorVis_Icon;
  defaultVisFlags |= 1 << DebugInspectorVis_Explicit;
  defaultVisFlags |= 1 << DebugInspectorVis_Light;
  defaultVisFlags |= 1 << DebugInspectorVis_Collision;
  defaultVisFlags |= 1 << DebugInspectorVis_Locomotion;
  defaultVisFlags |= 1 << DebugInspectorVis_NavigationPath;
  defaultVisFlags |= 1 << DebugInspectorVis_NavigationGrid;

  return ecs_world_add_t(
      w,
      global,
      DebugInspectorSettingsComp,
      .visFlags     = defaultVisFlags,
      .visMode      = DebugInspectorVisMode_Default,
      .tool         = DebugInspectorTool_Translation,
      .toolRotation = geo_quat_ident);
}

static const AssetPrefabMapComp* inspector_prefab_map(EcsWorld* w, const ScenePrefabEnvComp* p) {
  EcsView*     mapView = ecs_world_view_t(w, PrefabMapView);
  EcsIterator* mapItr  = ecs_view_maybe_at(mapView, scene_prefab_map(p));
  return mapItr ? ecs_view_read_t(mapItr, AssetPrefabMapComp) : null;
}

ecs_system_define(DebugInspectorUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalPanelUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp*        time     = ecs_view_read_t(globalItr, SceneTimeComp);
  SceneSetEnvComp*            setEnv   = ecs_view_write_t(globalItr, SceneSetEnvComp);
  DebugInspectorSettingsComp* settings = inspector_settings_get_or_create(world);
  DebugStatsGlobalComp*       stats    = ecs_view_write_t(globalItr, DebugStatsGlobalComp);
  DebugFinderComp*            finder   = ecs_view_write_t(globalItr, DebugFinderComp);

  ScenePrefabEnvComp*       prefabEnv = ecs_view_write_t(globalItr, ScenePrefabEnvComp);
  const AssetPrefabMapComp* prefabMap = inspector_prefab_map(world, prefabEnv);

  const StringHash selectedSet = g_sceneSetSelected;

  EcsView*     subjectView = ecs_world_view_t(world, SubjectView);
  EcsIterator* subjectItr  = ecs_view_maybe_at(subjectView, scene_set_main(setEnv, selectedSet));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId        entity    = ecs_view_entity(itr);
    DebugInspectorPanelComp* panelComp = ecs_view_write_t(itr, DebugInspectorPanelComp);
    UiCanvasComp*            canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
      continue;
    }
    InspectorContext ctx = {
        .world          = world,
        .canvas         = canvas,
        .panel          = panelComp,
        .time           = time,
        .prefabEnv      = prefabEnv,
        .prefabMap      = prefabMap,
        .setEnv         = setEnv,
        .stats          = stats,
        .settings       = settings,
        .finder         = finder,
        .scriptAssetItr = ecs_view_itr(ecs_world_view_t(world, ScriptAssetView)),
        .entityRefItr   = ecs_view_itr(ecs_world_view_t(world, EntityRefView)),
        .subject        = subjectItr,
        .subjectEntity  = subjectItr ? ecs_view_entity(subjectItr) : 0,
        .isEditMode     = inspector_is_edit_variant(subjectItr),
    };
    inspector_panel_draw(&ctx);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

static void inspector_tool_toggle(DebugInspectorSettingsComp* set, const DebugInspectorTool tool) {
  if (set->tool != tool) {
    set->tool = tool;
  } else {
    set->tool = DebugInspectorTool_None;
  }
}

static void inspector_tool_destroy(EcsWorld* w, const SceneSetEnvComp* setEnv) {
  const StringHash s = g_sceneSetSelected;
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    if (ecs_world_exists(w, *e)) {
      ecs_world_entity_destroy(w, *e);
    }
  }
}

static void
inspector_tool_drop(EcsWorld* w, const SceneSetEnvComp* setEnv, const SceneTerrainComp* terrain) {
  if (!scene_terrain_loaded(terrain)) {
    return;
  }
  const StringHash s   = g_sceneSetSelected;
  EcsIterator*     itr = ecs_view_itr(ecs_world_view_t(w, SubjectView));
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    if (!ecs_view_maybe_jump(itr, *e)) {
      continue; // Selected entity is missing required components.
    }
    scene_terrain_snap(terrain, &ecs_view_write_t(itr, SceneTransformComp)->position);
  }
}

static void inspector_tool_duplicate(EcsWorld* w, SceneSetEnvComp* setEnv) {
  EcsIterator* itr = ecs_view_itr(ecs_world_view_t(w, SubjectView));

  const StringHash s = g_sceneSetSelected;
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    if (ecs_view_maybe_jump(itr, *e)) {
      inspector_prefab_duplicate(w, itr);
    }
  }
  /**
   * Clear the old selection (the newly created entities will be automatically selected due to
   * duplicating the sets of the original entities).
   */
  scene_set_clear(setEnv, s);
}

static void inspector_tool_select_all(EcsWorld* w, SceneSetEnvComp* setEnv) {
  const u32    compCount       = ecs_def_comp_count(ecs_world_def(w));
  const BitSet ignoredCompMask = mem_stack(bits_to_bytes(compCount) + 1);

  // Setup ignored components.
  bitset_clear_all(ignoredCompMask);
  bitset_set(ignoredCompMask, ecs_comp_id(SceneCameraComp));

  scene_set_clear(setEnv, g_sceneSetSelected);

  EcsView* subjectView = ecs_world_view_t(w, SubjectView);
  for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
    const EcsEntityId    e         = ecs_view_entity(itr);
    const EcsArchetypeId archetype = ecs_world_entity_archetype(w, e);
    if (bitset_any_of(ecs_world_component_mask(w, archetype), ignoredCompMask)) {
      continue;
    }
    scene_set_add(setEnv, g_sceneSetSelected, e, SceneSetFlags_None);
  }
}

static GeoVector inspector_tool_pivot(EcsWorld* w, const SceneSetEnvComp* setEnv) {
  EcsIterator*     itr = ecs_view_itr(ecs_world_view_t(w, SubjectView));
  GeoVector        pivot;
  u32              count = 0;
  const StringHash s     = g_sceneSetSelected;
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    if (ecs_view_maybe_jump(itr, *e)) {
      const SceneTransformComp* transComp = ecs_view_read_t(itr, SceneTransformComp);
      pivot = count ? geo_vector_add(pivot, transComp->position) : transComp->position;
      ++count;
    }
  }
  return count ? geo_vector_div(pivot, count) : geo_vector(0);
}

static void inspector_tool_group_update(
    EcsWorld*                   w,
    DebugInspectorSettingsComp* set,
    const SceneSetEnvComp*      setEnv,
    DebugGizmoComp*             gizmo) {
  EcsIterator* itr = ecs_view_itr(ecs_world_view_t(w, SubjectView));
  if (!ecs_view_maybe_jump(itr, scene_set_main(setEnv, g_sceneSetSelected))) {
    return; // No main selected entity or its missing required components.
  }
  const SceneTransformComp* mainTrans = ecs_view_read_t(itr, SceneTransformComp);
  const SceneScaleComp*     mainScale = ecs_view_read_t(itr, SceneScaleComp);

  const GeoVector pos   = inspector_tool_pivot(w, setEnv);
  const f32       scale = mainScale ? mainScale->scale : 1.0f;

  if (set->space == DebugInspectorSpace_Local) {
    set->toolRotation = mainTrans->rotation;
  }

  static const DebugGizmoId g_groupGizmoId = 1234567890;

  GeoVector posEdit   = pos;
  GeoQuat   rotEdit   = set->toolRotation;
  f32       scaleEdit = scale;
  bool      posDirty = false, rotDirty = false, scaleDirty = false;
  switch (set->tool) {
  case DebugInspectorTool_Translation:
    posDirty |= debug_gizmo_translation(gizmo, g_groupGizmoId, &posEdit, set->toolRotation);
    break;
  case DebugInspectorTool_Rotation:
    rotDirty |= debug_gizmo_rotation(gizmo, g_groupGizmoId, pos, &rotEdit);
    break;
  case DebugInspectorTool_Scale:
    /**
     * Disable scaling if the main selected entity has no scale, reason is in that case we have no
     * reference for the delta computation and the editing won't be stable across frames.
     */
    if (mainScale) {
      scaleDirty |= debug_gizmo_scale_uniform(gizmo, g_groupGizmoId, pos, &scaleEdit);
    }
    break;
  default:
    break;
  }
  if (posDirty | rotDirty | scaleDirty) {
    const GeoVector  posDelta   = geo_vector_sub(posEdit, pos);
    const GeoQuat    rotDelta   = geo_quat_from_to(set->toolRotation, rotEdit);
    const f32        scaleDelta = scaleEdit / scale;
    const StringHash s          = g_sceneSetSelected;
    for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
      if (ecs_view_maybe_jump(itr, *e)) {
        SceneTransformComp* transform = ecs_view_write_t(itr, SceneTransformComp);
        SceneScaleComp*     scaleComp = ecs_view_write_t(itr, SceneScaleComp);
        if (posDirty) {
          transform->position = geo_vector_add(transform->position, posDelta);
        }
        if (rotDirty) {
          scene_transform_rotate_around(transform, pos, rotDelta);
        }
        if (scaleComp && scaleDirty) {
          scene_transform_scale_around(transform, scaleComp, pos, scaleDelta);
        }
      }
    }
    set->toolRotation = rotEdit;
  } else {
    set->toolRotation = geo_quat_ident;
  }
}

static void inspector_tool_individual_update(
    EcsWorld*                   w,
    DebugInspectorSettingsComp* set,
    const SceneSetEnvComp*      setEnv,
    DebugGizmoComp*             gizmo) {
  EcsIterator*     itr = ecs_view_itr(ecs_world_view_t(w, SubjectView));
  const StringHash s   = g_sceneSetSelected;

  bool rotActive = false;
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    if (ecs_view_maybe_jump(itr, *e)) {
      const DebugGizmoId  gizmoId   = (DebugGizmoId)ecs_view_entity(itr);
      SceneTransformComp* trans     = ecs_view_write_t(itr, SceneTransformComp);
      SceneScaleComp*     scaleComp = ecs_view_write_t(itr, SceneScaleComp);

      GeoQuat rotRef;
      if (set->space == DebugInspectorSpace_Local) {
        rotRef = trans->rotation;
      } else if (debug_gizmo_interacting(gizmo, gizmoId)) {
        rotRef = set->toolRotation;
      } else {
        rotRef = geo_quat_ident;
      }
      GeoQuat rotEdit = rotRef;

      switch (set->tool) {
      case DebugInspectorTool_Translation:
        debug_gizmo_translation(gizmo, gizmoId, &trans->position, rotRef);
        break;
      case DebugInspectorTool_Rotation:
        if (debug_gizmo_rotation(gizmo, gizmoId, trans->position, &rotEdit)) {
          const GeoQuat rotDelta = geo_quat_from_to(rotRef, rotEdit);
          scene_transform_rotate_around(trans, trans->position, rotDelta);
          set->toolRotation = rotEdit;
          rotActive         = true;
        }
        break;
      case DebugInspectorTool_Scale:
        if (scaleComp) {
          debug_gizmo_scale_uniform(gizmo, gizmoId, trans->position, &scaleComp->scale);
        }
        break;
      default:
        break;
      }
    }
  }
  if (!rotActive) {
    set->toolRotation = geo_quat_ident;
  }
}

typedef struct {
  EcsWorld*    world;
  EcsIterator* entityRefItr;
} ToolPickerQueryContext;

static bool tool_picker_query_filter(const void* ctx, const EcsEntityId entity, const u32 layer) {
  (void)layer;
  const ToolPickerQueryContext* c = ctx;
  if (!ecs_world_has_t(c->world, entity, SceneLevelInstanceComp)) {
    return false;
  }
  ecs_view_jump(c->entityRefItr, entity);
  const ScenePrefabInstanceComp* inst = ecs_view_read_t(c->entityRefItr, ScenePrefabInstanceComp);
  if (!inst || inst->isVolatile) {
    return false;
  }
  return true;
}

static void inspector_tool_picker_update(
    EcsWorld*                    world,
    DebugInspectorSettingsComp*  set,
    DebugStatsGlobalComp*        stats,
    DebugShapeComp*              shape,
    DebugTextComp*               text,
    const InputManagerComp*      input,
    const SceneCollisionEnvComp* collisionEnv,
    EcsIterator*                 cameraItr,
    EcsIterator*                 entityRefItr) {

  bool shouldClose = false;
  shouldClose |= cameraItr == null;
  shouldClose |= input_triggered_lit(input, "DebugInspectorPickerClose");

  if (shouldClose) {
    set->tool = set->toolPickerPrevTool;
    debug_stats_notify(stats, string_lit("Tool"), g_toolNames[set->tool]);
    return;
  }

  const SceneCameraComp*    camera      = ecs_view_read_t(cameraItr, SceneCameraComp);
  const SceneTransformComp* cameraTrans = ecs_view_read_t(cameraItr, SceneTransformComp);

  const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
  const f32       inputAspect  = input_cursor_aspect(input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);

  const ToolPickerQueryContext filterCtx = {.world = world, .entityRefItr = entityRefItr};
  const SceneQueryFilter       filter    = {
               .context   = &filterCtx,
               .callback  = &tool_picker_query_filter,
               .layerMask = SceneLayer_AllIncludingDebug,
  };

  String      hitName = string_lit("< None >");
  SceneRayHit hit;
  if (scene_query_ray(collisionEnv, &inputRay, 1e5f /* maxDist */, &filter, &hit)) {
    if (ecs_view_maybe_jump(entityRefItr, hit.entity)) {
      set->toolPickerResult = hit.entity;

      const SceneNameComp*      nameComp   = ecs_view_read_t(entityRefItr, SceneNameComp);
      const SceneBoundsComp*    boundsComp = ecs_view_read_t(entityRefItr, SceneBoundsComp);
      const SceneTransformComp* transComp  = ecs_view_read_t(entityRefItr, SceneTransformComp);
      const SceneScaleComp*     scaleComp  = ecs_view_read_t(entityRefItr, SceneScaleComp);
      if (nameComp) {
        hitName = stringtable_lookup(g_stringtable, nameComp->name);
        if (transComp) {
          debug_text(text, transComp->position, hitName, .fontSize = 16);
        }
      }
      const GeoColor shapeColor = geo_color(0, 0.5f, 0, 0.6f);
      if (boundsComp) {
        const GeoBoxRotated b      = scene_bounds_world_rotated(boundsComp, transComp, scaleComp);
        const GeoVector     center = geo_box_center(&b.box);
        const GeoVector     size   = geo_box_size(&b.box);
        const GeoVector     sizeDilated = geo_vector_add(size, geo_vector(0.1f, 0.1f, 0.1f));
        debug_box(shape, center, b.rotation, sizeDilated, shapeColor, DebugShape_Fill);
      } else if (transComp) {
        debug_sphere(shape, transComp->position, 1.0f /* radius */, shapeColor, DebugShape_Fill);
      }
    } else {
      set->toolPickerResult = ecs_entity_invalid;
    }
  } else {
    set->toolPickerResult = ecs_entity_invalid;
  }
  debug_stats_notify(stats, string_lit("Picker entity"), hitName);
}

ecs_system_define(DebugInspectorToolUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalToolUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  InputManagerComp*            input        = ecs_view_write_t(globalItr, InputManagerComp);
  const SceneTerrainComp*      terrain      = ecs_view_read_t(globalItr, SceneTerrainComp);
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  SceneSetEnvComp*             setEnv       = ecs_view_write_t(globalItr, SceneSetEnvComp);
  DebugShapeComp*              shape        = ecs_view_write_t(globalItr, DebugShapeComp);
  DebugTextComp*               text         = ecs_view_write_t(globalItr, DebugTextComp);
  DebugGizmoComp*              gizmo        = ecs_view_write_t(globalItr, DebugGizmoComp);
  DebugInspectorSettingsComp*  set   = ecs_view_write_t(globalItr, DebugInspectorSettingsComp);
  DebugStatsGlobalComp*        stats = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  if (!input_layer_active(input, string_hash_lit("Debug"))) {
    if (set->tool == DebugInspectorTool_Picker) {
      set->tool = set->toolPickerPrevTool;
      input_blocker_update(input, InputBlocker_EntityPicker, false);
    }
    return; // Tools are only active in debug mode.
  }
  if (input_triggered_lit(input, "DebugInspectorToolTranslation")) {
    inspector_tool_toggle(set, DebugInspectorTool_Translation);
    debug_stats_notify(stats, string_lit("Tool"), g_toolNames[set->tool]);
  }
  if (input_triggered_lit(input, "DebugInspectorToolRotation")) {
    inspector_tool_toggle(set, DebugInspectorTool_Rotation);
    debug_stats_notify(stats, string_lit("Tool"), g_toolNames[set->tool]);
  }
  if (input_triggered_lit(input, "DebugInspectorToolScale")) {
    inspector_tool_toggle(set, DebugInspectorTool_Scale);
    debug_stats_notify(stats, string_lit("Tool"), g_toolNames[set->tool]);
  }
  if (input_triggered_lit(input, "DebugInspectorToggleSpace")) {
    set->space = (set->space + 1) % DebugInspectorSpace_Count;
    debug_stats_notify(stats, string_lit("Space"), g_spaceNames[set->space]);
  }
  if (input_triggered_lit(input, "DebugInspectorToggleNavLayer")) {
    set->visNavLayer = (set->visNavLayer + 1) % SceneNavLayer_Count;
    debug_stats_notify(stats, string_lit("Space"), g_sceneNavLayerNames[set->visNavLayer]);
  }
  if (input_triggered_lit(input, "DebugInspectorDestroy")) {
    inspector_tool_destroy(world, setEnv);
    debug_stats_notify(stats, string_lit("Tool"), string_lit("Destroy"));
  }
  if (input_triggered_lit(input, "DebugInspectorDrop")) {
    inspector_tool_drop(world, setEnv, terrain);
    debug_stats_notify(stats, string_lit("Tool"), string_lit("Drop"));
  }
  if (input_triggered_lit(input, "DebugInspectorDuplicate")) {
    inspector_tool_duplicate(world, setEnv);
    debug_stats_notify(stats, string_lit("Tool"), string_lit("Duplicate"));
  }
  if (input_triggered_lit(input, "DebugInspectorSelectAll")) {
    inspector_tool_select_all(world, setEnv);
    debug_stats_notify(stats, string_lit("Tool"), string_lit("Select all"));
  }

  input_blocker_update(input, InputBlocker_EntityPicker, set->tool == DebugInspectorTool_Picker);

  EcsView*     cameraView   = ecs_world_view_t(world, CameraView);
  EcsIterator* cameraItr    = ecs_view_maybe_at(cameraView, input_active_window(input));
  EcsIterator* entityRefItr = ecs_view_itr(ecs_world_view_t(world, EntityRefView));

  switch (set->tool) {
  case DebugInspectorTool_None:
  case DebugInspectorTool_Count:
    break;
  case DebugInspectorTool_Translation:
  case DebugInspectorTool_Rotation:
  case DebugInspectorTool_Scale:
    if (input_modifiers(input) & InputModifier_Control) {
      inspector_tool_individual_update(world, set, setEnv, gizmo);
    } else {
      inspector_tool_group_update(world, set, setEnv, gizmo);
    }
    break;
  case DebugInspectorTool_Picker:
    inspector_tool_picker_update(
        world, set, stats, shape, text, input, collisionEnv, cameraItr, entityRefItr);
    break;
  }
}

static void inspector_vis_draw_locomotion(
    DebugShapeComp*            shape,
    const SceneLocomotionComp* loco,
    const SceneTransformComp*  transform,
    const SceneScaleComp*      scale) {
  const GeoVector pos      = transform ? transform->position : geo_vector(0);
  const f32       scaleVal = scale ? scale->scale : 1.0f;

  const f32      sepThreshold = loco->radius * 0.25f;
  const f32      sepFrac      = math_min(math_sqrt_f32(loco->lastSepMagSqr) / sepThreshold, 1.0f);
  const GeoColor sepColor     = geo_color_lerp(geo_color_white, geo_color_red, sepFrac);

  debug_circle(shape, pos, geo_quat_up_to_forward, loco->radius * scaleVal, sepColor);

  if (loco->flags & SceneLocomotion_Moving) {
    debug_line(shape, pos, loco->targetPos, geo_color_yellow);
    debug_sphere(shape, loco->targetPos, 0.1f, geo_color_green, DebugShape_Overlay);
  }
  if (geo_vector_mag_sqr(loco->targetDir) > f32_epsilon) {
    debug_arrow(shape, pos, geo_vector_add(pos, loco->targetDir), 0.1f, geo_color_teal);
  }
}

static void inspector_vis_draw_collision(
    DebugShapeComp*           shape,
    const SceneCollisionComp* collision,
    const SceneTransformComp* transform,
    const SceneScaleComp*     scale) {

  for (u32 i = 0; i != collision->shapeCount; ++i) {
    const SceneCollisionShape* local = &collision->shapes[i];
    const SceneCollisionShape  world = scene_collision_shape_world(local, transform, scale);

    switch (world.type) {
    case SceneCollisionType_Sphere:
      debug_world_sphere(shape, &world.sphere, geo_color(1, 0, 0, 0.75f));
      break;
    case SceneCollisionType_Capsule:
      debug_world_capsule(shape, &world.capsule, geo_color(1, 0, 0, 0.75f));
      break;
    case SceneCollisionType_Box:
      debug_world_box_rotated(shape, &world.box, geo_color(1, 0, 0, 0.75f));
      break;
    case SceneCollisionType_Count:
      UNREACHABLE
    }
  }
}

static void inspector_vis_draw_bounds_local(
    DebugShapeComp*           shape,
    const SceneBoundsComp*    bounds,
    const SceneTransformComp* transform,
    const SceneScaleComp*     scale) {
  const GeoBoxRotated b = scene_bounds_world_rotated(bounds, transform, scale);
  debug_world_box_rotated(shape, &b, geo_color(0, 1, 0, 1.0f));
}

static void inspector_vis_draw_bounds_global(
    DebugShapeComp*           shape,
    const SceneBoundsComp*    bounds,
    const SceneTransformComp* transform,
    const SceneScaleComp*     scale) {
  const GeoBox b = scene_bounds_world(bounds, transform, scale);
  debug_world_box(shape, &b, geo_color(0, 0, 1, 1.0f));
}

static void inspector_vis_draw_navigation_path(
    DebugShapeComp*           shape,
    const SceneNavEnvComp*    nav,
    const SceneNavAgentComp*  agent,
    const SceneNavPathComp*   path,
    const SceneTransformComp* transform) {
  const GeoNavGrid* grid = scene_nav_grid(nav, path->layer);
  for (u32 i = 1; i < path->cellCount; ++i) {
    const GeoVector posA = geo_nav_position(grid, path->cells[i - 1]);
    const GeoVector posB = geo_nav_position(grid, path->cells[i]);
    debug_line(shape, posA, posB, geo_color_white);
  }
  if (agent->flags & SceneNavAgent_Traveling) {
    debug_sphere(shape, agent->targetPos, 0.1f, geo_color_blue, DebugShape_Overlay);

    const f32 channelRadius = geo_nav_channel_radius(grid);
    debug_circle(shape, transform->position, geo_quat_up_to_forward, channelRadius, geo_color_blue);
  }
}

static void inspector_vis_draw_light_point(
    DebugShapeComp*            shape,
    const SceneLightPointComp* lightPoint,
    const SceneTransformComp*  transform,
    const SceneScaleComp*      scaleComp) {
  const GeoVector pos    = transform ? transform->position : geo_vector(0);
  const f32       radius = scaleComp ? lightPoint->radius * scaleComp->scale : lightPoint->radius;
  debug_sphere(shape, pos, radius, geo_color(1, 1, 1, 0.25f), DebugShape_Wire);
}

static void inspector_vis_draw_light_dir(
    DebugShapeComp* shape, const SceneLightDirComp* lightDir, const SceneTransformComp* transform) {
  (void)lightDir;
  const GeoVector pos      = transform ? transform->position : geo_vector(0);
  const GeoQuat   rot      = transform ? transform->rotation : geo_quat_ident;
  const GeoVector dir      = geo_quat_rotate(rot, geo_forward);
  const GeoVector arrowEnd = geo_vector_add(pos, geo_vector_mul(dir, 5));
  debug_arrow(shape, pos, arrowEnd, 0.75f, geo_color(1, 1, 1, 0.5f));
}

static void inspector_vis_draw_health(
    DebugTextComp* text, const SceneHealthComp* health, const SceneTransformComp* transform) {
  const GeoVector pos          = transform ? transform->position : geo_vector(0);
  const f32       healthPoints = scene_health_points(health);
  const GeoColor  color        = geo_color_lerp(geo_color_red, geo_color_lime, health->norm);
  const String    str = fmt_write_scratch("{}", fmt_float(healthPoints, .maxDecDigits = 0));
  debug_text(text, pos, str, .color = color, .fontSize = 16);
}

static void inspector_vis_draw_attack(
    DebugShapeComp*             shape,
    DebugTextComp*              text,
    const SceneAttackComp*      attack,
    const SceneAttackTraceComp* trace,
    const SceneTransformComp*   transform) {

  const f32 readyPct = math_round_nearest_f32(attack->readyNorm * 100.0f);
  debug_text(text, transform->position, fmt_write_scratch("Ready: {}%", fmt_float(readyPct)));

  const SceneAttackEvent* eventsBegin = scene_attack_trace_begin(trace);
  const SceneAttackEvent* eventsEnd   = scene_attack_trace_end(trace);

  for (const SceneAttackEvent* itr = eventsBegin; itr != eventsEnd; ++itr) {
    switch (itr->type) {
    case SceneAttackEventType_Proj: {
      const SceneAttackEventProj* evt = &itr->data_proj;
      debug_line(shape, evt->pos, evt->target, geo_color_blue);
    } break;
    case SceneAttackEventType_DmgSphere: {
      const SceneAttackEventDmgSphere* evt = &itr->data_dmgSphere;
      debug_sphere(shape, evt->pos, evt->radius, geo_color_blue, DebugShape_Wire);
    } break;
    case SceneAttackEventType_DmgFrustum: {
      const SceneAttackEventDmgFrustum* evt = &itr->data_dmgFrustum;
      debug_frustum_points(shape, evt->corners, geo_color_blue);
    } break;
    }
  }
}

static void inspector_vis_draw_target(
    DebugTextComp*               text,
    const SceneTargetFinderComp* tgtFinder,
    const SceneTargetTraceComp*  tgtTrace,
    EcsView*                     transformView) {

  DynString             textBuffer      = dynstring_create_over(mem_stack(32));
  const FormatOptsFloat formatOptsFloat = format_opts_float(.minDecDigits = 0, .maxDecDigits = 2);

  EcsIterator* transformItr = ecs_view_itr(transformView);

  const SceneTargetScore* scoresBegin = scene_target_trace_begin(tgtTrace);
  const SceneTargetScore* scoresEnd   = scene_target_trace_end(tgtTrace);

  for (const SceneTargetScore* itr = scoresBegin; itr != scoresEnd; ++itr) {
    if (ecs_view_maybe_jump(transformItr, itr->entity)) {
      const GeoVector pos = ecs_view_read_t(transformItr, SceneTransformComp)->position;

      GeoColor color;
      if (itr->value <= 0) {
        color = geo_color(1, 1, 1, 0.25f);
      } else if (itr->entity == scene_target_primary(tgtFinder)) {
        color = geo_color_lime;
      } else if (scene_target_contains(tgtFinder, itr->entity)) {
        color = geo_color_fuchsia;
      } else {
        color = geo_color_white;
      }

      dynstring_clear(&textBuffer);
      format_write_f64(&textBuffer, itr->value, &formatOptsFloat);

      debug_text(text, pos, dynstring_view(&textBuffer), .color = color);
    }
  }
}

static void inspector_vis_draw_vision(
    DebugShapeComp* shape, const SceneVisionComp* vision, const SceneTransformComp* transform) {
  debug_circle(
      shape,
      transform->position,
      geo_quat_forward_to_up,
      vision->radius,
      geo_color_soothing_purple);
}

static void inspector_vis_draw_location(
    DebugShapeComp*           shape,
    const SceneLocationComp*  location,
    const SceneTransformComp* transform,
    const SceneScaleComp*     scale) {
  for (SceneLocationType type = 0; type != SceneLocationType_Count; ++type) {
    const GeoBoxRotated volume = scene_location(location, transform, scale, type);
    const GeoVector     center = geo_box_center(&volume.box);
    const GeoVector     size   = geo_box_size(&volume.box);
    const GeoColor      color  = geo_color_for(type);
    debug_box(shape, center, volume.rotation, size, color, DebugShape_Wire);
    debug_sphere(shape, center, 0.1f, color, DebugShape_Overlay);
  }
}

static void inspector_vis_draw_explicit(
    DebugShapeComp* shape, DebugTextComp* text, const SceneDebugComp* comp) {
  const SceneDebug* debugData  = scene_debug_data(comp);
  const usize       debugCount = scene_debug_count(comp);
  for (usize i = 0; i != debugCount; ++i) {
    switch (debugData[i].type) {
    case SceneDebugType_Line: {
      const SceneDebugLine* data = &debugData[i].data_line;
      debug_line(shape, data->start, data->end, data->color);
    } break;
    case SceneDebugType_Sphere: {
      const SceneDebugSphere* data = &debugData[i].data_sphere;
      debug_sphere(shape, data->pos, data->radius, data->color, DebugShape_Overlay);
    } break;
    case SceneDebugType_Box: {
      const SceneDebugBox* data = &debugData[i].data_box;
      debug_box(shape, data->pos, data->rot, data->size, data->color, DebugShape_Overlay);
    } break;
    case SceneDebugType_Arrow: {
      const SceneDebugArrow* data = &debugData[i].data_arrow;
      debug_arrow(shape, data->start, data->end, data->radius, data->color);
    } break;
    case SceneDebugType_Orientation: {
      const SceneDebugOrientation* data = &debugData[i].data_orientation;
      debug_orientation(shape, data->pos, data->rot, data->size);
    } break;
    case SceneDebugType_Text: {
      const SceneDebugText* data = &debugData[i].data_text;
      debug_text(text, data->pos, data->text, .color = data->color, .fontSize = data->fontSize);
    } break;
    case SceneDebugType_Trace:
      break;
    }
  }
}

static void inspector_vis_draw_subject(
    DebugShapeComp*                   shape,
    DebugTextComp*                    text,
    const DebugInspectorSettingsComp* set,
    const SceneNavEnvComp*            nav,
    EcsIterator*                      subject) {
  const SceneAttackTraceComp* attackTraceComp = ecs_view_read_t(subject, SceneAttackTraceComp);
  const SceneBoundsComp*      boundsComp      = ecs_view_read_t(subject, SceneBoundsComp);
  const SceneCollisionComp*   collisionComp   = ecs_view_read_t(subject, SceneCollisionComp);
  const SceneHealthComp*      healthComp      = ecs_view_read_t(subject, SceneHealthComp);
  const SceneLightDirComp*    lightDirComp    = ecs_view_read_t(subject, SceneLightDirComp);
  const SceneLightPointComp*  lightPointComp  = ecs_view_read_t(subject, SceneLightPointComp);
  const SceneLocationComp*    locationComp    = ecs_view_read_t(subject, SceneLocationComp);
  const SceneLocomotionComp*  locoComp        = ecs_view_read_t(subject, SceneLocomotionComp);
  const SceneNameComp*        nameComp        = ecs_view_read_t(subject, SceneNameComp);
  const SceneNavAgentComp*    navAgentComp    = ecs_view_read_t(subject, SceneNavAgentComp);
  const SceneNavPathComp*     navPathComp     = ecs_view_read_t(subject, SceneNavPathComp);
  const SceneScaleComp*       scaleComp       = ecs_view_read_t(subject, SceneScaleComp);
  const SceneTransformComp*   transformComp   = ecs_view_read_t(subject, SceneTransformComp);
  const SceneVelocityComp*    veloComp        = ecs_view_read_t(subject, SceneVelocityComp);
  const SceneVisionComp*      visionComp      = ecs_view_read_t(subject, SceneVisionComp);
  SceneAttackComp*            attackComp      = ecs_view_write_t(subject, SceneAttackComp);

  if (transformComp && set->visFlags & (1 << DebugInspectorVis_Origin)) {
    debug_sphere(shape, transformComp->position, 0.05f, geo_color_fuchsia, DebugShape_Overlay);
    debug_orientation(shape, transformComp->position, transformComp->rotation, 0.25f);

    if (veloComp && geo_vector_mag(veloComp->velocityAvg) > 1e-3f) {
      const GeoVector posOneSecAway = scene_position_predict(transformComp, veloComp, time_second);
      debug_arrow(shape, transformComp->position, posOneSecAway, 0.15f, geo_color_green);
    }
  }
  if (nameComp && set->visFlags & (1 << DebugInspectorVis_Name)) {
    const String    name = stringtable_lookup(g_stringtable, nameComp->name);
    const GeoVector pos  = geo_vector_add(transformComp->position, geo_vector_mul(geo_up, 0.1f));
    debug_text(text, pos, name);
  }
  if (locoComp && set->visFlags & (1 << DebugInspectorVis_Locomotion)) {
    inspector_vis_draw_locomotion(shape, locoComp, transformComp, scaleComp);
  }
  if (collisionComp && set->visFlags & (1 << DebugInspectorVis_Collision)) {
    inspector_vis_draw_collision(shape, collisionComp, transformComp, scaleComp);
  }
  if (boundsComp && !geo_box_is_inverted3(&boundsComp->local)) {
    if (set->visFlags & (1 << DebugInspectorVis_BoundsLocal)) {
      inspector_vis_draw_bounds_local(shape, boundsComp, transformComp, scaleComp);
    }
    if (set->visFlags & (1 << DebugInspectorVis_BoundsGlobal)) {
      inspector_vis_draw_bounds_global(shape, boundsComp, transformComp, scaleComp);
    }
  }
  if (navAgentComp && navPathComp && set->visFlags & (1 << DebugInspectorVis_NavigationPath)) {
    inspector_vis_draw_navigation_path(shape, nav, navAgentComp, navPathComp, transformComp);
  }
  if (lightPointComp && set->visFlags & (1 << DebugInspectorVis_Light)) {
    inspector_vis_draw_light_point(shape, lightPointComp, transformComp, scaleComp);
  }
  if (lightDirComp && set->visFlags & (1 << DebugInspectorVis_Light)) {
    inspector_vis_draw_light_dir(shape, lightDirComp, transformComp);
  }
  if (healthComp && set->visFlags & (1 << DebugInspectorVis_Health)) {
    inspector_vis_draw_health(text, healthComp, transformComp);
  }
  if (attackComp && set->visFlags & (1 << DebugInspectorVis_Attack)) {
    attackComp->flags |= SceneAttackFlags_Trace; // Enable diagnostic tracing for this entity.
    if (attackTraceComp) {
      inspector_vis_draw_attack(shape, text, attackComp, attackTraceComp, transformComp);
    }
  }
  if (visionComp && transformComp && set->visFlags & (1 << DebugInspectorVis_Vision)) {
    inspector_vis_draw_vision(shape, visionComp, transformComp);
  }
  if (locationComp && transformComp && set->visFlags & (1 << DebugInspectorVis_Location)) {
    inspector_vis_draw_location(shape, locationComp, transformComp, scaleComp);
  }
}

static GeoNavRegion inspector_nav_encapsulate(const GeoNavRegion region, const GeoNavCell cell) {
  return (GeoNavRegion){
      .min.x = math_min(region.min.x, cell.x),
      .min.y = math_min(region.min.y, cell.y),
      .max.x = math_max(region.max.x, cell.x + 1), // +1 because max is exclusive.
      .max.y = math_max(region.max.y, cell.y + 1), // +1 because max is exclusive.
  };
}

static GeoNavRegion inspector_nav_visible_region(const GeoNavGrid* grid, EcsView* cameraView) {
  static const GeoPlane  g_groundPlane     = {.normal = {.y = 1.0f}};
  static const GeoVector g_screenCorners[] = {
      {.x = 0, .y = 0},
      {.x = 0, .y = 1},
      {.x = 1, .y = 1},
      {.x = 1, .y = 0},
  };

  GeoNavRegion result      = {.min = {.x = u16_max, .y = u16_max}};
  bool         resultValid = false;

  for (EcsIterator* itr = ecs_view_itr(cameraView); ecs_view_walk(itr);) {
    const GapWindowAspectComp* winAspect = ecs_view_read_t(itr, GapWindowAspectComp);
    const SceneCameraComp*     cam       = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp*  trans     = ecs_view_read_t(itr, SceneTransformComp);

    for (u32 i = 0; i != array_elems(g_screenCorners); ++i) {
      const GeoRay    ray  = scene_camera_ray(cam, trans, winAspect->ratio, g_screenCorners[i]);
      f32             rayT = geo_plane_intersect_ray(&g_groundPlane, &ray);
      const GeoVector pos  = geo_ray_position(&ray, rayT < f32_epsilon ? 1e4f : rayT);
      result               = inspector_nav_encapsulate(result, geo_nav_at_position(grid, pos));
    }
    resultValid = true;
  }

  return resultValid ? result : (GeoNavRegion){0};
}

static void inspector_vis_draw_navigation_grid(
    DebugShapeComp* shape, DebugTextComp* text, const GeoNavGrid* grid, EcsView* cameraView) {

  DynString textBuffer = dynstring_create_over(mem_stack(32));

  const f32          cellSize = geo_nav_cell_size(grid);
  const GeoNavRegion region   = inspector_nav_visible_region(grid, cameraView);

  const DebugShapeMode shapeMode = DebugShape_Overlay;
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    for (u32 x = region.min.x; x != region.max.x; ++x) {
      const GeoNavCell   cell     = {.x = x, .y = y};
      const GeoNavIsland island   = geo_nav_island(grid, cell);
      const bool         occupied = geo_nav_check(grid, cell, GeoNavCond_Occupied);
      const bool         blocked  = geo_nav_check(grid, cell, GeoNavCond_Blocked);

      if (island == 0 && !occupied && !blocked) {
        continue; // Skip drawing unblocked and un-occupied cells on the main island.
      }

      const bool occupiedMoving = geo_nav_check(grid, cell, GeoNavCond_OccupiedMoving);
      const bool highlight      = (x & 1) == (y & 1);

      GeoColor color;
      if (blocked) {
        color = geo_color(1, 0, 0, highlight ? 0.25f : 0.2f);
      } else if (occupiedMoving) {
        color = geo_color(1, 0, 1, highlight ? 0.15f : 0.1f);
      } else if (occupied) {
        color = geo_color(0, 0, 1, highlight ? 0.15f : 0.1f);
      } else {
        color = geo_color(0, 1, 0, highlight ? 0.075f : 0.05f);
      }
      const GeoVector pos = geo_nav_position(grid, cell);
      debug_quad(shape, pos, geo_quat_up_to_forward, cellSize, cellSize, color, shapeMode);

      if (!blocked) {
        dynstring_clear(&textBuffer);
        format_write_u64(&textBuffer, island, &format_opts_int());
        debug_text(text, pos, dynstring_view(&textBuffer));
      }
    }
  }
}

static void inspector_vis_draw_collision_bounds(DebugShapeComp* shape, const GeoQueryEnv* env) {
  const u32 nodeCount = geo_query_node_count(env);
  for (u32 nodeIdx = 0; nodeIdx != nodeCount; ++nodeIdx) {
    const GeoBox*   bounds = geo_query_node_bounds(env, nodeIdx);
    const u32       depth  = geo_query_node_depth(env, nodeIdx);
    const GeoVector center = geo_box_center(bounds);
    const GeoVector size   = geo_box_size(bounds);
    debug_box(shape, center, geo_quat_ident, size, geo_color_for(depth), DebugShape_Wire);
  }
}

static void inspector_vis_draw_icon(EcsWorld* w, DebugTextComp* text, EcsIterator* subject) {
  const SceneTransformComp* transformComp = ecs_view_read_t(subject, SceneTransformComp);
  const SceneSetMemberComp* setMember     = ecs_view_read_t(subject, SceneSetMemberComp);
  const SceneScriptComp*    scriptComp    = ecs_view_read_t(subject, SceneScriptComp);
  const EcsEntityId         e             = ecs_view_entity(subject);

  Unicode  icon;
  GeoColor color;
  u16      size;

  if (scriptComp && (scene_script_flags(scriptComp) & SceneScriptFlags_DidPanic) != 0) {
    icon  = UiShape_Error;
    color = geo_color(1.0f, 0, 0, 0.75f);
    size  = 25;
  } else {
    if (scriptComp || ecs_world_has_t(w, e, ScenePropertyComp)) {
      icon = UiShape_Description;
    } else if (ecs_world_has_t(w, e, DebugPrefabPreviewComp)) {
      icon = 0; // No icon for previews.
    } else if (ecs_world_has_t(w, e, SceneVfxDecalComp)) {
      icon = UiShape_Image;
    } else if (ecs_world_has_t(w, e, SceneVfxSystemComp)) {
      icon = UiShape_Grain;
    } else if (ecs_world_has_t(w, e, SceneLightPointComp)) {
      icon = UiShape_Light;
    } else if (ecs_world_has_t(w, e, SceneLightDirComp)) {
      icon = UiShape_Light;
    } else if (ecs_world_has_t(w, e, SceneLightAmbientComp)) {
      icon = UiShape_Light;
    } else if (ecs_world_has_t(w, e, SceneSoundComp)) {
      icon = UiShape_MusicNote;
    } else if (ecs_world_has_t(w, e, SceneRenderableComp)) {
      icon = UiShape_WebAsset;
    } else if (ecs_world_has_t(w, e, SceneCollisionComp)) {
      icon = UiShape_Dashboard;
    } else if (ecs_world_has_t(w, e, SceneCameraComp)) {
      /**
       * Avoid drawing an icon for the camera as it will appear in the middle of the screen, another
       * approach would be modifying the text drawing to skip text very close to the screen.
       */
      icon = 0;
    } else {
      icon = '?';
    }
    color = geo_color(0.85f, 0.85f, 0.85f, 0.6f);
    size  = 20;
  }

  if (setMember && scene_set_member_contains(setMember, g_sceneSetSelected)) {
    color = geo_color_add(geo_color_with_alpha(color, 1.0), geo_color(0.25f, 0.25f, 0.25f, 0.0f));
  }

  if (icon) {
    u8           textBuffer[4];
    const String str = {.ptr = textBuffer, .size = utf8_cp_write(textBuffer, icon)};

    debug_text(text, transformComp->position, str, .fontSize = size, .color = color);
  }
}

ecs_system_define(DebugInspectorVisDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalVisDrawView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const InputManagerComp*     input = ecs_view_read_t(globalItr, InputManagerComp);
  DebugInspectorSettingsComp* set   = ecs_view_write_t(globalItr, DebugInspectorSettingsComp);
  DebugStatsGlobalComp*       stats = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  if (!set->drawVisInGame && !input_layer_active(input, string_hash_lit("Debug"))) {
    return;
  }

  static const String g_drawHotkeys[DebugInspectorVis_Count] = {
      [DebugInspectorVis_Icon]           = string_static("DebugInspectorVisIcon"),
      [DebugInspectorVis_Name]           = string_static("DebugInspectorVisName"),
      [DebugInspectorVis_Collision]      = string_static("DebugInspectorVisCollision"),
      [DebugInspectorVis_Locomotion]     = string_static("DebugInspectorVisLocomotion"),
      [DebugInspectorVis_NavigationPath] = string_static("DebugInspectorVisNavigationPath"),
      [DebugInspectorVis_NavigationGrid] = string_static("DebugInspectorVisNavigationGrid"),
      [DebugInspectorVis_Light]          = string_static("DebugInspectorVisLight"),
      [DebugInspectorVis_Vision]         = string_static("DebugInspectorVisVision"),
      [DebugInspectorVis_Health]         = string_static("DebugInspectorVisHealth"),
      [DebugInspectorVis_Attack]         = string_static("DebugInspectorVisAttack"),
      [DebugInspectorVis_Target]         = string_static("DebugInspectorVisTarget"),
  };
  for (DebugInspectorVis vis = 0; vis != DebugInspectorVis_Count; ++vis) {
    const u32 hotKeyHash = string_hash(g_drawHotkeys[vis]);
    if (hotKeyHash && input_triggered_hash(input, hotKeyHash)) {
      set->visFlags ^= (1 << vis);
      inspector_notify_vis(set, stats, vis);
    }
  }

  if (input_triggered_hash(input, string_hash_lit("DebugInspectorVisMode"))) {
    set->visMode = (set->visMode + 1) % DebugInspectorVisMode_Count;
    inspector_notify_vis_mode(stats, set->visMode);
  }

  if (!set->visFlags) {
    return;
  }
  const SceneNavEnvComp*       navEnv       = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneSetEnvComp*       setEnv       = ecs_view_read_t(globalItr, SceneSetEnvComp);
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  DebugShapeComp*              shape        = ecs_view_write_t(globalItr, DebugShapeComp);
  DebugTextComp*               text         = ecs_view_write_t(globalItr, DebugTextComp);

  EcsView*     transformView = ecs_world_view_t(world, TransformView);
  EcsView*     subjectView   = ecs_world_view_t(world, SubjectView);
  EcsView*     cameraView    = ecs_world_view_t(world, CameraView);
  EcsIterator* subjectItr    = ecs_view_itr(subjectView);

  if (set->visFlags & (1 << DebugInspectorVis_NavigationGrid)) {
    trace_begin("debug_vis_grid", TraceColor_Red);
    const GeoNavGrid* grid = scene_nav_grid(navEnv, set->visNavLayer);
    inspector_vis_draw_navigation_grid(shape, text, grid, cameraView);
    trace_end();
  }
  if (set->visFlags & (1 << DebugInspectorVis_CollisionBounds)) {
    trace_begin("debug_vis_collision_bounds", TraceColor_Red);
    inspector_vis_draw_collision_bounds(shape, scene_collision_query_env(collisionEnv));
    trace_end();
  }
  if (set->visFlags & (1 << DebugInspectorVis_Icon)) {
    trace_begin("debug_vis_icon", TraceColor_Red);
    for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
      inspector_vis_draw_icon(world, text, itr);
    }
    trace_end();
  }
  if (set->visFlags & (1 << DebugInspectorVis_Explicit)) {
    for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
      const SceneDebugComp* debugComp = ecs_view_read_t(itr, SceneDebugComp);
      if (debugComp) {
        inspector_vis_draw_explicit(shape, text, debugComp);
      }
    }
  }
  switch (set->visMode) {
  case DebugInspectorVisMode_SelectedOnly: {
    const StringHash s = g_sceneSetSelected;
    for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
      if (ecs_view_maybe_jump(subjectItr, *e)) {
        inspector_vis_draw_subject(shape, text, set, navEnv, subjectItr);
      }
    }
  } break;
  case DebugInspectorVisMode_All: {
    for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
      inspector_vis_draw_subject(shape, text, set, navEnv, itr);
    }
  } break;
  case DebugInspectorVisMode_Count:
    UNREACHABLE
  }
  if (set->visFlags & (1 << DebugInspectorVis_Target)) {
    if (ecs_view_maybe_jump(subjectItr, scene_set_main(setEnv, g_sceneSetSelected))) {
      SceneTargetFinderComp* tgtFinder = ecs_view_write_t(subjectItr, SceneTargetFinderComp);
      if (tgtFinder) {
        tgtFinder->config |= SceneTargetConfig_Trace; // Enable diagnostic tracing for this entity.

        const SceneTargetTraceComp* tgtTrace = ecs_view_read_t(subjectItr, SceneTargetTraceComp);
        if (tgtTrace) {
          inspector_vis_draw_target(text, tgtFinder, tgtTrace, transformView);
        }
      }
    }
  }
}

ecs_module_init(debug_inspector_module) {
  ecs_register_comp(DebugInspectorSettingsComp);
  ecs_register_comp(DebugInspectorPanelComp, .destructor = ecs_destruct_panel_comp);

  ecs_register_view(SettingsWriteView);
  ecs_register_view(GlobalPanelUpdateView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(GlobalToolUpdateView);
  ecs_register_view(GlobalVisDrawView);
  ecs_register_view(SubjectView);
  ecs_register_view(TransformView);
  ecs_register_view(ScriptAssetView);
  ecs_register_view(EntityRefView);
  ecs_register_view(CameraView);
  ecs_register_view(PrefabMapView);

  ecs_register_system(
      DebugInspectorUpdatePanelSys,
      ecs_view_id(GlobalPanelUpdateView),
      ecs_view_id(SettingsWriteView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView),
      ecs_view_id(ScriptAssetView),
      ecs_view_id(EntityRefView),
      ecs_view_id(PrefabMapView));

  ecs_register_system(
      DebugInspectorToolUpdateSys,
      ecs_view_id(GlobalToolUpdateView),
      ecs_view_id(SubjectView),
      ecs_view_id(CameraView),
      ecs_view_id(EntityRefView));

  ecs_register_system(
      DebugInspectorVisDrawSys,
      ecs_view_id(GlobalVisDrawView),
      ecs_view_id(SubjectView),
      ecs_view_id(TransformView),
      ecs_view_id(CameraView));

  ecs_order(DebugInspectorToolUpdateSys, DebugOrder_InspectorToolUpdate);
  ecs_order(DebugInspectorVisDrawSys, DebugOrder_InspectorDebugDraw);
}

EcsEntityId
debug_inspector_panel_open(EcsWorld* world, const EcsEntityId window, const DebugPanelType type) {
  const EcsEntityId        panelEntity    = debug_panel_create(world, window, type);
  DebugInspectorPanelComp* inspectorPanel = ecs_world_add_t(
      world,
      panelEntity,
      DebugInspectorPanelComp,
      .panel         = ui_panel(.position = ui_vector(0.0f, 0.0f), .size = ui_vector(500, 500)),
      .newSetBuffer  = dynstring_create(g_allocHeap, 0),
      .newPropBuffer = dynstring_create(g_allocHeap, 0));

  inspectorPanel->newPropVal = inspector_panel_prop_default(inspectorPanel->newPropType);

  if (type == DebugPanelType_Detached) {
    ui_panel_maximize(&inspectorPanel->panel);
  }

  return panelEntity;
}
