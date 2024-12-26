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
#include "debug_gizmo.h"
#include "debug_inspector.h"
#include "debug_prefab.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_stats.h"
#include "debug_text.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "geo_capsule.h"
#include "geo_query.h"
#include "geo_sphere.h"
#include "input_manager.h"
#include "scene_attachment.h"
#include "scene_attack.h"
#include "scene_bounds.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_debug.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_light.h"
#include "scene_location.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_nav.h"
#include "scene_prefab.h"
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
#include "trace_tracer.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"

#include "widget_internal.h"

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

ecs_comp_define(DebugInspectorSettingsComp) {
  DebugInspectorSpace   space;
  DebugInspectorTool    tool;
  DebugInspectorVisMode visMode;
  SceneNavLayer         visNavLayer;
  u32                   visFlags;
  bool                  drawVisInGame;
  GeoQuat               toolRotation; // Cached rotation to support world-space rotation tools.
};

ecs_comp_define(DebugInspectorPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  u32          totalRows;
  DynString    setNameBuffer;
  GeoVector    transformRotEulerDeg; // Local copy of rotation as euler angles to use while editing.
};

static void ecs_destruct_panel_comp(void* data) {
  DebugInspectorPanelComp* panel = data;
  dynstring_destroy(&panel->setNameBuffer);
}

ecs_view_define(SettingsWriteView) { ecs_access_write(DebugInspectorSettingsComp); }

ecs_view_define(GlobalPanelUpdateView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_read(ScenePrefabEnvComp);
  ecs_access_write(DebugStatsGlobalComp);
  ecs_access_write(SceneSetEnvComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugInspectorPanelComp's are exclusively managed here.

  ecs_access_read(DebugPanelComp);
  ecs_access_write(DebugInspectorPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(GlobalToolUpdateView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(DebugGizmoComp);
  ecs_access_write(DebugInspectorSettingsComp);
  ecs_access_write(DebugStatsGlobalComp);
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
  ecs_access_maybe_read(SceneLocomotionComp);
  ecs_access_maybe_read(SceneNameComp);
  ecs_access_maybe_read(SceneNavAgentComp);
  ecs_access_maybe_read(SceneNavPathComp);
  ecs_access_maybe_read(ScenePrefabInstanceComp);
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
  ecs_access_maybe_write(SceneRenderableComp);
  ecs_access_maybe_write(SceneScaleComp);
  ecs_access_maybe_write(SceneTagComp);
  ecs_access_maybe_write(SceneTargetFinderComp);
  ecs_access_maybe_write(SceneVfxDecalComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(TransformView) { ecs_access_read(SceneTransformComp); }

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

static bool inspector_panel_section(UiCanvasComp* canvas, const String label) {
  bool open;
  ui_layout_push(canvas);
  {
    ui_layout_move_to(canvas, UiBase_Container, UiAlign_MiddleLeft, Ui_X);
    ui_layout_resize_to(canvas, UiBase_Container, UiAlign_MiddleRight, Ui_X);

    ui_style_push(canvas);
    {
      ui_style_color(canvas, ui_color(0, 0, 0, 128));
      ui_style_outline(canvas, 2);
      ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_None);
    }
    ui_style_pop(canvas);

    ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);
    open = ui_section(canvas, .label = label);
  }
  ui_layout_pop(canvas);
  return open;
}

static void inspector_panel_next(UiCanvasComp* cv, DebugInspectorPanelComp* panel, UiTable* table) {
  ui_table_next_row(cv, table);
  ++panel->totalRows;
}

static void inspector_panel_draw_value_string(UiCanvasComp* canvas, const String value) {
  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);
  ui_label(canvas, value, .selectable = true);
  ui_style_pop(canvas);
}

static void inspector_panel_draw_value_entity(UiCanvasComp* canvas, const EcsEntityId value) {
  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);
  ui_label_entity(canvas, value);
  ui_style_pop(canvas);
}

static void inspector_panel_draw_value_none(UiCanvasComp* canvas) {
  ui_style_push(canvas);
  ui_style_color_mult(canvas, 0.75f);
  inspector_panel_draw_value_string(canvas, string_lit("< None >"));
  ui_style_pop(canvas);
}

static void inspector_panel_draw_entity_info(
    EcsWorld*                world,
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  inspector_panel_next(canvas, panelComp, table);
  ui_label(canvas, string_lit("Entity identifier"));
  ui_table_next_column(canvas, table);
  if (subject) {
    const EcsEntityId entity = ecs_view_entity(subject);
    inspector_panel_draw_value_entity(canvas, entity);
  } else {
    inspector_panel_draw_value_none(canvas);
  }

  inspector_panel_next(canvas, panelComp, table);
  ui_label(canvas, string_lit("Entity name"));
  ui_table_next_column(canvas, table);
  if (subject) {
    const SceneNameComp* nameComp = ecs_view_read_t(subject, SceneNameComp);
    if (nameComp) {
      inspector_panel_draw_value_string(canvas, stringtable_lookup(g_stringtable, nameComp->name));
    }
  } else {
    inspector_panel_draw_value_none(canvas);
  }

  inspector_panel_next(canvas, panelComp, table);
  ui_label(canvas, string_lit("Entity archetype"));
  ui_table_next_column(canvas, table);
  if (subject) {
    const EcsArchetypeId archetype = ecs_world_entity_archetype(world, ecs_view_entity(subject));
    if (!(sentinel_check(archetype))) {
      inspector_panel_draw_value_string(canvas, fmt_write_scratch("{}", fmt_int(archetype)));
    }
  } else {
    inspector_panel_draw_value_none(canvas);
  }
}

static void inspector_panel_draw_prefab_instance(
    UiCanvasComp*             canvas,
    DebugInspectorPanelComp*  panelComp,
    const AssetPrefabMapComp* prefabMap,
    UiTable*                  table,
    EcsIterator*              subject) {
  const ScenePrefabInstanceComp* instance =
      subject ? ecs_view_read_t(subject, ScenePrefabInstanceComp) : null;
  if (instance) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Prefab"))) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Id"));
      ui_table_next_column(canvas, table);
      inspector_panel_draw_value_string(canvas, fmt_write_scratch("{}", fmt_int(instance->id)));

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Prefab"));
      ui_table_next_column(canvas, table);
      StringHash prefabId = instance->prefabId; // TODO: Support switching prefabs.
      debug_widget_editor_prefab(canvas, prefabMap, &prefabId);
    }
  }
}

static void inspector_panel_draw_transform(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneTransformComp* transform = subject ? ecs_view_write_t(subject, SceneTransformComp) : null;
  SceneScaleComp*     scale     = subject ? ecs_view_write_t(subject, SceneScaleComp) : null;
  if (!transform && !scale) {
    return;
  }
  inspector_panel_next(canvas, panelComp, table);
  if (!inspector_panel_section(canvas, string_lit("Transform"))) {
    return;
  }
  if (transform) {
    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Position"));
    ui_table_next_column(canvas, table);
    if (debug_widget_editor_vec3_resettable(canvas, &transform->position, UiWidget_Default)) {
      // Clamp the position to a sane value.
      transform->position = geo_vector_clamp(transform->position, 1e3f);
    }

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Rotation"));
    ui_table_next_column(canvas, table);
    if (debug_widget_editor_vec3_resettable(
            canvas, &panelComp->transformRotEulerDeg, UiWidget_DirtyWhileEditing)) {
      const GeoVector eulerRad = geo_vector_mul(panelComp->transformRotEulerDeg, math_deg_to_rad);
      transform->rotation      = geo_quat_from_euler(eulerRad);
    } else {
      const GeoVector eulerRad        = geo_quat_to_euler(transform->rotation);
      panelComp->transformRotEulerDeg = geo_vector_mul(eulerRad, math_rad_to_deg);
    }
  }
  if (scale) {
    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Scale"));
    ui_table_next_column(canvas, table);
    if (debug_widget_editor_f32(canvas, &scale->scale, UiWidget_Default)) {
      // Clamp the scale to a sane value.
      scale->scale = math_clamp_f32(scale->scale, 1e-2f, 1e2f);
    }
  }
}

static void inspector_panel_draw_light(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneLightPointComp*   point = subject ? ecs_view_write_t(subject, SceneLightPointComp) : null;
  SceneLightDirComp*     dir   = subject ? ecs_view_write_t(subject, SceneLightDirComp) : null;
  SceneLightAmbientComp* amb   = subject ? ecs_view_write_t(subject, SceneLightAmbientComp) : null;
  if (!point && !dir && !amb) {
    return;
  }
  inspector_panel_next(canvas, panelComp, table);
  if (inspector_panel_section(canvas, string_lit("Light"))) {
    if (point) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Radiance"));
      ui_table_next_column(canvas, table);
      debug_widget_editor_color(canvas, &point->radiance, UiWidget_Default);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Radius"));
      ui_table_next_column(canvas, table);
      if (debug_widget_editor_f32(canvas, &point->radius, UiWidget_Default)) {
        // Clamp the radius to a sane value.
        point->radius = math_clamp_f32(point->radius, 1e-3f, 1e3f);
      }
    }
    if (dir) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Radiance"));
      ui_table_next_column(canvas, table);
      debug_widget_editor_color(canvas, &dir->radiance, UiWidget_Default);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Shadows"));
      ui_table_next_column(canvas, table);
      ui_toggle(canvas, &dir->shadows);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Coverage"));
      ui_table_next_column(canvas, table);
      ui_toggle(canvas, &dir->coverage);
    }
    if (amb) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Ambient"));
      ui_table_next_column(canvas, table);
      if (debug_widget_editor_f32(canvas, &amb->intensity, UiWidget_Default)) {
        // Clamp the ambient intensity to a sane value.
        amb->intensity = math_clamp_f32(amb->intensity, 0.0f, 10.0f);
      }
    }
  }
}

static void inspector_panel_draw_health(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneHealthComp* health = subject ? ecs_view_write_t(subject, SceneHealthComp) : null;
  if (health) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Health"))) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Amount"));
      ui_table_next_column(canvas, table);
      ui_slider(canvas, &health->norm);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Max"));
      ui_table_next_column(canvas, table);
      debug_widget_editor_f32(canvas, &health->max, UiWidget_Default);
    }
  }
}

static void inspector_panel_draw_status(
    EcsWorld*                world,
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  const SceneStatusComp* status = subject ? ecs_view_read_t(subject, SceneStatusComp) : null;
  if (status) {
    inspector_panel_next(canvas, panelComp, table);
    const u32 activeCount = bits_popcnt((u32)status->active);
    if (inspector_panel_section(canvas, fmt_write_scratch("Status ({})", fmt_int(activeCount)))) {
      for (SceneStatusType type = 0; type != SceneStatusType_Count; ++type) {
        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, scene_status_name(type));
        ui_table_next_column(canvas, table);
        bool active = scene_status_active(status, type);
        if (ui_toggle(canvas, &active)) {
          if (active) {
            const EcsEntityId instigator = 0;
            scene_status_add(world, ecs_view_entity(subject), type, instigator);
          } else {
            scene_status_remove(world, ecs_view_entity(subject), type);
          }
        }
      }
    }
  }
}

static void inspector_panel_draw_faction(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneFactionComp* faction = subject ? ecs_view_write_t(subject, SceneFactionComp) : null;
  if (faction) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Faction"))) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Id"));
      ui_table_next_column(canvas, table);
      debug_widget_editor_faction(canvas, &faction->id);
    }
  }
}

static void inspector_panel_draw_target(
    const SceneTimeComp*     time,
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  const SceneTargetFinderComp* finder =
      subject ? ecs_view_read_t(subject, SceneTargetFinderComp) : null;
  if (finder) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Target"))) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Entity"));
      ui_table_next_column(canvas, table);
      inspector_panel_draw_value_entity(canvas, scene_target_primary(finder));

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Time until refresh"));
      ui_table_next_column(canvas, table);
      ui_label(canvas, fmt_write_scratch("{}", fmt_duration(finder->nextRefreshTime - time->time)));
    }
  }
}

static void inspector_panel_draw_nav_agent(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  const SceneNavAgentComp* agent = subject ? ecs_view_read_t(subject, SceneNavAgentComp) : null;
  if (agent) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Navigation Agent"))) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Layer"));
      ui_table_next_column(canvas, table);
      ui_select(canvas, (i32*)&agent->layer, g_sceneNavLayerNames, SceneNavLayer_Count);
    }
  }
}

static void inspector_panel_draw_renderable(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneRenderableComp* renderable = subject ? ecs_view_write_t(subject, SceneRenderableComp) : null;
  if (renderable) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Renderable"))) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Graphic"));
      ui_table_next_column(canvas, table);
      inspector_panel_draw_value_entity(canvas, renderable->graphic);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Color"));
      ui_table_next_column(canvas, table);
      debug_widget_editor_color(canvas, &renderable->color, UiWidget_Default);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Emissive"));
      ui_table_next_column(canvas, table);
      ui_slider(canvas, &renderable->emissive);
    }
  }
}

static void inspector_panel_draw_decal(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneVfxDecalComp* decal = subject ? ecs_view_write_t(subject, SceneVfxDecalComp) : null;
  if (decal) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Decal"))) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Alpha"));
      ui_table_next_column(canvas, table);
      ui_slider(canvas, &decal->alpha);
    }
  }
}

static void inspector_panel_draw_sets(
    SceneSetEnvComp*         setEnv,
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  if (!subject) {
    return;
  }
  const SceneSetMemberComp* setMember = ecs_view_read_t(subject, SceneSetMemberComp);

  StringHash sets[scene_set_member_max_sets];
  const u32  setCount = setMember ? scene_set_member_all(setMember, sets) : 0;

  inspector_panel_next(canvas, panelComp, table);
  if (inspector_panel_section(canvas, fmt_write_scratch("Sets ({})", fmt_int(setCount)))) {
    for (u32 i = 0; i != setCount; ++i) {
      inspector_panel_next(canvas, panelComp, table);
      const String setName = stringtable_lookup(g_stringtable, sets[i]);
      ui_label(canvas, string_is_empty(setName) ? string_lit("< unknown >") : setName);
      ui_table_next_column(canvas, table);
      ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(25, 0), UiBase_Absolute, Ui_X);
      if (ui_button(
              canvas,
              .label      = ui_shape_scratch(UiShape_Delete),
              .fontSize   = 18,
              .frameColor = ui_color(255, 16, 0, 192),
              .tooltip    = string_lit("Remove this entity from the set."))) {
        scene_set_remove(setEnv, sets[i], ecs_view_entity(subject));
      }
    }

    inspector_panel_next(canvas, panelComp, table);
    ui_textbox(canvas, &panelComp->setNameBuffer, .placeholder = string_lit("Set name..."));
    ui_table_next_column(canvas, table);
    ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(25, 0), UiBase_Absolute, Ui_X);
    if (ui_button(
            canvas,
            .flags      = panelComp->setNameBuffer.size == 0 ? UiWidget_Disabled : 0,
            .label      = ui_shape_scratch(UiShape_Add),
            .fontSize   = 18,
            .frameColor = ui_color(16, 192, 0, 192),
            .tooltip    = string_lit("Add this entity to the specified set."))) {
      const String     setName = dynstring_view(&panelComp->setNameBuffer);
      const StringHash set     = stringtable_add(g_stringtable, setName);
      scene_set_add(setEnv, set, ecs_view_entity(subject), SceneSetFlags_None);
      dynstring_clear(&panelComp->setNameBuffer);
    }
  }
}

static void inspector_panel_draw_tags(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneTagComp* tagComp = subject ? ecs_view_write_t(subject, SceneTagComp) : null;
  if (tagComp) {
    const u32 tagCount = bits_popcnt((u32)tagComp->tags);
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, fmt_write_scratch("Tags ({})", fmt_int(tagCount)))) {
      for (u32 i = 0; i != SceneTags_Count; ++i) {
        const SceneTags tag = 1 << i;
        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, scene_tag_name(tag));
        ui_table_next_column(canvas, table);
        ui_toggle_flag(canvas, (u32*)&tagComp->tags, tag);
      }
    }
  }
}

static void inspector_panel_draw_collision(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneCollisionComp* col = subject ? ecs_view_write_t(subject, SceneCollisionComp) : null;
  if (col) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Collision"))) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Layer"));
      ui_table_next_column(canvas, table);
      if (bits_popcnt((u32)col->layer) == 1) {
        inspector_panel_draw_value_string(canvas, scene_layer_name(col->layer));
      } else {
        inspector_panel_draw_value_string(canvas, string_lit("< Multiple >"));
      }

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Shapes"));
      ui_table_next_column(canvas, table);
      inspector_panel_draw_value_string(canvas, fmt_write_scratch("{}", fmt_int(col->shapeCount)));

      for (u32 i = 0; i != col->shapeCount; ++i) {
        SceneCollisionShape* shape = &col->shapes[i];

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, fmt_write_scratch("[{}]\tType", fmt_int(i)));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_value_string(canvas, scene_collision_type_name(shape->type));

        switch (shape->type) {
        case SceneCollisionType_Sphere: {
          inspector_panel_next(canvas, panelComp, table);
          ui_label(canvas, string_lit("\tOffset"));
          ui_table_next_column(canvas, table);
          debug_widget_editor_vec3(canvas, &shape->sphere.point, UiWidget_Default);

          inspector_panel_next(canvas, panelComp, table);
          ui_label(canvas, string_lit("\tRadius"));
          ui_table_next_column(canvas, table);
          debug_widget_editor_f32(canvas, &shape->sphere.radius, UiWidget_Default);
        } break;
        case SceneCollisionType_Capsule: {
          inspector_panel_next(canvas, panelComp, table);
          ui_label(canvas, string_lit("\tA"));
          ui_table_next_column(canvas, table);
          debug_widget_editor_vec3(canvas, &shape->capsule.line.a, UiWidget_Default);

          inspector_panel_next(canvas, panelComp, table);
          ui_label(canvas, string_lit("\tB"));
          ui_table_next_column(canvas, table);
          debug_widget_editor_vec3(canvas, &shape->capsule.line.b, UiWidget_Default);

          inspector_panel_next(canvas, panelComp, table);
          ui_label(canvas, string_lit("\tRadius"));
          ui_table_next_column(canvas, table);
          debug_widget_editor_f32(canvas, &shape->capsule.radius, UiWidget_Default);
        } break;
        case SceneCollisionType_Box: {
          inspector_panel_next(canvas, panelComp, table);
          ui_label(canvas, string_lit("\tMin"));
          ui_table_next_column(canvas, table);
          debug_widget_editor_vec3(canvas, &shape->box.box.min, UiWidget_Default);

          inspector_panel_next(canvas, panelComp, table);
          ui_label(canvas, string_lit("\tMax"));
          ui_table_next_column(canvas, table);
          debug_widget_editor_vec3(canvas, &shape->box.box.max, UiWidget_Default);
        } break;
        case SceneCollisionType_Count:
          UNREACHABLE
        }
      }
    }
  }
}

static void inspector_panel_draw_bounds(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneBoundsComp* boundsComp = subject ? ecs_view_write_t(subject, SceneBoundsComp) : null;
  if (boundsComp) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Bounds"))) {
      GeoVector center = geo_box_center(&boundsComp->local);
      GeoVector size   = geo_box_size(&boundsComp->local);
      bool      dirty  = false;

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Center"));
      ui_table_next_column(canvas, table);
      dirty |= debug_widget_editor_vec3(canvas, &center, UiWidget_Default);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Size"));
      ui_table_next_column(canvas, table);
      dirty |= debug_widget_editor_vec3(canvas, &size, UiWidget_Default);

      if (dirty) {
        boundsComp->local = geo_box_from_center(center, size);
      }
    }
  }
}

static void inspector_panel_draw_location(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneLocationComp* location = subject ? ecs_view_write_t(subject, SceneLocationComp) : null;
  if (location) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Location"))) {

      for (SceneLocationType type = 0; type != SceneLocationType_Count; ++type) {
        const String typeName = scene_location_type_name(type);

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, fmt_write_scratch("{} Min", fmt_text(typeName)));
        ui_table_next_column(canvas, table);
        debug_widget_editor_vec3(canvas, &location->volumes[type].min, UiWidget_Default);

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, fmt_write_scratch("{} Max", fmt_text(typeName)));
        ui_table_next_column(canvas, table);
        debug_widget_editor_vec3(canvas, &location->volumes[type].max, UiWidget_Default);
      }
    }
  }
}

static void inspector_panel_draw_attachment(
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  SceneAttachmentComp* attach = subject ? ecs_view_write_t(subject, SceneAttachmentComp) : null;
  if (attach) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Attachment"))) {

      DynString jointName = dynstring_create(g_allocScratch, 64);
      if (attach->jointName) {
        dynstring_append(&jointName, stringtable_lookup(g_stringtable, attach->jointName));
      }

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Joint"));
      ui_table_next_column(canvas, table);
      if (ui_textbox(canvas, &jointName, .maxTextLength = 64)) {
        attach->jointIndex = sentinel_u32;
        attach->jointName  = string_maybe_hash(dynstring_view(&jointName));
      }

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Offset"));
      ui_table_next_column(canvas, table);
      debug_widget_editor_vec3(canvas, &attach->offset, UiWidget_Default);
    }
  }
}

static void inspector_panel_draw_components(
    EcsWorld*                world,
    UiCanvasComp*            canvas,
    DebugInspectorPanelComp* panelComp,
    UiTable*                 table,
    EcsIterator*             subject) {
  if (!subject) {
    return;
  }
  const EcsArchetypeId archetype = ecs_world_entity_archetype(world, ecs_view_entity(subject));
  const BitSet         compMask  = ecs_world_component_mask(world, archetype);
  const u32            compCount = (u32)bitset_count(compMask);

  inspector_panel_next(canvas, panelComp, table);
  if (inspector_panel_section(canvas, fmt_write_scratch("Components ({})", fmt_int(compCount)))) {
    const EcsDef* def = ecs_world_def(world);
    bitset_for(compMask, compId) {
      const String compName = ecs_def_comp_name(def, (EcsCompId)compId);
      const usize  compSize = ecs_def_comp_size(def, (EcsCompId)compId);
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, compName);
      ui_table_next_column(canvas, table);
      inspector_panel_draw_value_string(
          canvas, fmt_write_scratch("id: {<3} size: {}", fmt_int(compId), fmt_size(compSize)));
    }
  }
}

static void inspector_panel_draw_settings(
    UiCanvasComp*               canvas,
    DebugStatsGlobalComp*       stats,
    DebugInspectorPanelComp*    panelComp,
    UiTable*                    table,
    DebugInspectorSettingsComp* settings) {
  inspector_panel_next(canvas, panelComp, table);
  if (inspector_panel_section(canvas, string_lit("Settings"))) {
    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Space"));
    ui_table_next_column(canvas, table);
    if (ui_select(canvas, (i32*)&settings->space, g_spaceNames, array_elems(g_spaceNames))) {
      debug_stats_notify(stats, string_lit("Space"), g_spaceNames[settings->space]);
    }

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Tool"));
    ui_table_next_column(canvas, table);
    if (ui_select(canvas, (i32*)&settings->tool, g_toolNames, array_elems(g_toolNames))) {
      debug_stats_notify(stats, string_lit("Tool"), g_toolNames[settings->tool]);
    }

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Visualize In Game"));
    ui_table_next_column(canvas, table);
    ui_toggle(canvas, &settings->drawVisInGame);

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Navigation Layer"));
    ui_table_next_column(canvas, table);
    const String* layerNames = g_sceneNavLayerNames;
    if (ui_select(canvas, (i32*)&settings->visNavLayer, layerNames, SceneNavLayer_Count)) {
      debug_stats_notify(stats, string_lit("Navigation Layer"), layerNames[settings->visNavLayer]);
    }

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Visualize Mode"));
    ui_table_next_column(canvas, table);
    ui_select(canvas, (i32*)&settings->visMode, g_visModeNames, array_elems(g_visModeNames));

    for (DebugInspectorVis vis = 0; vis != DebugInspectorVis_Count; ++vis) {
      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, fmt_write_scratch("Visualize {}", fmt_text(g_visNames[vis])));
      ui_table_next_column(canvas, table);
      if (ui_toggle_flag(canvas, (u32*)&settings->visFlags, 1 << vis)) {
        inspector_notify_vis(settings, stats, vis);
      }
    }
  }
}

static void inspector_panel_draw(
    EcsWorld*                   world,
    DebugStatsGlobalComp*       stats,
    const SceneTimeComp*        time,
    SceneSetEnvComp*            setEnv,
    const AssetPrefabMapComp*   prefabMap,
    UiCanvasComp*               canvas,
    DebugInspectorPanelComp*    panelComp,
    DebugInspectorSettingsComp* settings,
    EcsIterator*                subject) {
  const String title = fmt_write_scratch("{} Inspector Panel", fmt_ui_shape(ViewInAr));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 215);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const f32 totalHeight = ui_table_height(&table, panelComp->totalRows);
  ui_scrollview_begin(canvas, &panelComp->scrollview, UiLayer_Normal, totalHeight);
  panelComp->totalRows = 0;

  /**
   * NOTE: The sections draw a variable amount of elements, thus we jump to the next id block
   * afterwards to keep consistent ids.
   */

  inspector_panel_draw_entity_info(world, canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_prefab_instance(canvas, panelComp, prefabMap, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_transform(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_light(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_health(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_status(world, canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_faction(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_target(time, canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_nav_agent(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_renderable(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_decal(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_sets(setEnv, canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_tags(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_collision(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_location(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_attachment(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_bounds(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_components(world, canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_settings(canvas, stats, panelComp, &table, settings);
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_panel_end(canvas, &panelComp->panel);
}

static DebugInspectorSettingsComp* inspector_settings_get_or_create(EcsWorld* world) {
  const EcsEntityId global = ecs_world_global(world);
  EcsView*          view   = ecs_world_view_t(world, SettingsWriteView);
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
      world,
      global,
      DebugInspectorSettingsComp,
      .visFlags     = defaultVisFlags,
      .visMode      = DebugInspectorVisMode_Default,
      .tool         = DebugInspectorTool_Translation,
      .toolRotation = geo_quat_ident);
}

static const AssetPrefabMapComp*
inspector_prefab_map(EcsWorld* world, const ScenePrefabEnvComp* prefabEnv) {
  EcsView*     mapView = ecs_world_view_t(world, PrefabMapView);
  EcsIterator* mapItr  = ecs_view_maybe_at(mapView, scene_prefab_map(prefabEnv));
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

  const ScenePrefabEnvComp* prefabEnv = ecs_view_read_t(globalItr, ScenePrefabEnvComp);
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
    inspector_panel_draw(
        world, stats, time, setEnv, prefabMap, canvas, panelComp, settings, subjectItr);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

static void
debug_inspector_tool_toggle(DebugInspectorSettingsComp* set, const DebugInspectorTool tool) {
  if (set->tool != tool) {
    set->tool = tool;
  } else {
    set->tool = DebugInspectorTool_None;
  }
}

static void debug_inspector_tool_destroy(EcsWorld* world, const SceneSetEnvComp* setEnv) {
  const StringHash s = g_sceneSetSelected;
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    if (ecs_world_exists(world, *e)) {
      ecs_world_entity_destroy(world, *e);
    }
  }
}

static void debug_inspector_tool_drop(
    EcsWorld* world, const SceneSetEnvComp* setEnv, const SceneTerrainComp* terrain) {
  if (!scene_terrain_loaded(terrain)) {
    return;
  }
  const StringHash s   = g_sceneSetSelected;
  EcsIterator*     itr = ecs_view_itr(ecs_world_view_t(world, SubjectView));
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    if (!ecs_view_maybe_jump(itr, *e)) {
      continue; // Selected entity is missing required components.
    }
    scene_terrain_snap(terrain, &ecs_view_write_t(itr, SceneTransformComp)->position);
  }
}

static void debug_inspector_tool_duplicate(EcsWorld* world, SceneSetEnvComp* setEnv) {
  const StringHash s = g_sceneSetSelected;

  DynArray     newEntities = dynarray_create_t(g_allocHeap, EcsEntityId, 64);
  EcsIterator* itr         = ecs_view_itr(ecs_world_view_t(world, SubjectView));
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    if (!ecs_view_maybe_jump(itr, *e)) {
      continue; // Selected entity is missing required components.
    }
    const SceneTransformComp*      transComp      = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*          scaleComp      = ecs_view_read_t(itr, SceneScaleComp);
    const SceneFactionComp*        factionComp    = ecs_view_read_t(itr, SceneFactionComp);
    const ScenePrefabInstanceComp* prefabInstComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);
    if (!prefabInstComp || prefabInstComp->variant != ScenePrefabVariant_Preview) {
      continue; // Only non-preview prefab instances can be duplicated.
    }
    const EcsEntityId duplicatedEntity = scene_prefab_spawn(
        world,
        &(ScenePrefabSpec){
            .id       = prefabInstComp->id,
            .prefabId = prefabInstComp->prefabId,
            .variant  = prefabInstComp->variant,
            .faction  = factionComp ? factionComp->id : SceneFaction_None,
            .scale    = scaleComp ? scaleComp->scale : 1.0f,
            .position = transComp->position,
            .rotation = transComp->rotation,
        });
    *dynarray_push_t(&newEntities, EcsEntityId) = duplicatedEntity;
  }

  // Select the newly created entities.
  scene_set_clear(setEnv, s);
  dynarray_for_t(&newEntities, EcsEntityId, e) { scene_set_add(setEnv, s, *e, SceneSetFlags_None); }
  dynarray_destroy(&newEntities);
}

static void debug_inspector_tool_select_all(EcsWorld* world, SceneSetEnvComp* setEnv) {
  const u32    compCount       = ecs_def_comp_count(ecs_world_def(world));
  const BitSet ignoredCompMask = mem_stack(bits_to_bytes(compCount) + 1);

  // Setup ignored components.
  bitset_clear_all(ignoredCompMask);
  bitset_set(ignoredCompMask, ecs_comp_id(SceneCameraComp));

  scene_set_clear(setEnv, g_sceneSetSelected);

  EcsView* subjectView = ecs_world_view_t(world, SubjectView);
  for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
    const EcsEntityId    e         = ecs_view_entity(itr);
    const EcsArchetypeId archetype = ecs_world_entity_archetype(world, e);
    if (bitset_any_of(ecs_world_component_mask(world, archetype), ignoredCompMask)) {
      continue;
    }
    scene_set_add(setEnv, g_sceneSetSelected, e, SceneSetFlags_None);
  }
}

static GeoVector debug_inspector_tool_pivot(EcsWorld* world, const SceneSetEnvComp* setEnv) {
  EcsIterator*     itr = ecs_view_itr(ecs_world_view_t(world, SubjectView));
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

static void debug_inspector_tool_group_update(
    EcsWorld*                   world,
    DebugInspectorSettingsComp* set,
    const SceneSetEnvComp*      setEnv,
    DebugGizmoComp*             gizmo) {
  EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, SubjectView));
  if (!ecs_view_maybe_jump(itr, scene_set_main(setEnv, g_sceneSetSelected))) {
    return; // No main selected entity or its missing required components.
  }
  const SceneTransformComp* mainTrans = ecs_view_read_t(itr, SceneTransformComp);
  const SceneScaleComp*     mainScale = ecs_view_read_t(itr, SceneScaleComp);

  const GeoVector pos   = debug_inspector_tool_pivot(world, setEnv);
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
     * reference for the delta computation and the editing wont be stable across frames.
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

static void debug_inspector_tool_individual_update(
    EcsWorld*                   world,
    DebugInspectorSettingsComp* set,
    const SceneSetEnvComp*      setEnv,
    DebugGizmoComp*             gizmo) {
  EcsIterator*     itr = ecs_view_itr(ecs_world_view_t(world, SubjectView));
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

ecs_system_define(DebugInspectorToolUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalToolUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const InputManagerComp*     input   = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneTerrainComp*     terrain = ecs_view_read_t(globalItr, SceneTerrainComp);
  SceneSetEnvComp*            setEnv  = ecs_view_write_t(globalItr, SceneSetEnvComp);
  DebugGizmoComp*             gizmo   = ecs_view_write_t(globalItr, DebugGizmoComp);
  DebugInspectorSettingsComp* set     = ecs_view_write_t(globalItr, DebugInspectorSettingsComp);
  DebugStatsGlobalComp*       stats   = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  if (!input_layer_active(input, string_hash_lit("Debug"))) {
    return; // Gizmos are only active in debug mode.
  }
  if (input_triggered_lit(input, "DebugInspectorToolTranslation")) {
    debug_inspector_tool_toggle(set, DebugInspectorTool_Translation);
    debug_stats_notify(stats, string_lit("Tool"), g_toolNames[set->tool]);
  }
  if (input_triggered_lit(input, "DebugInspectorToolRotation")) {
    debug_inspector_tool_toggle(set, DebugInspectorTool_Rotation);
    debug_stats_notify(stats, string_lit("Tool"), g_toolNames[set->tool]);
  }
  if (input_triggered_lit(input, "DebugInspectorToolScale")) {
    debug_inspector_tool_toggle(set, DebugInspectorTool_Scale);
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
    debug_inspector_tool_destroy(world, setEnv);
    debug_stats_notify(stats, string_lit("Tool"), string_lit("Destroy"));
  }
  if (input_triggered_lit(input, "DebugInspectorDrop")) {
    debug_inspector_tool_drop(world, setEnv, terrain);
    debug_stats_notify(stats, string_lit("Tool"), string_lit("Drop"));
  }
  if (input_triggered_lit(input, "DebugInspectorDuplicate")) {
    debug_inspector_tool_duplicate(world, setEnv);
    debug_stats_notify(stats, string_lit("Tool"), string_lit("Duplicate"));
  }
  if (input_triggered_lit(input, "DebugInspectorSelectAll")) {
    debug_inspector_tool_select_all(world, setEnv);
    debug_stats_notify(stats, string_lit("Tool"), string_lit("Select all"));
  }

  if (set->tool != DebugInspectorTool_None) {
    if (input_modifiers(input) & InputModifier_Control) {
      debug_inspector_tool_individual_update(world, set, setEnv, gizmo);
    } else {
      debug_inspector_tool_group_update(world, set, setEnv, gizmo);
    }
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

static void inspector_vis_draw_icon(EcsWorld* world, DebugTextComp* text, EcsIterator* subject) {
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
    if (scriptComp || ecs_world_has_t(world, e, SceneKnowledgeComp)) {
      icon = UiShape_Description;
    } else if (ecs_world_has_t(world, e, DebugPrefabPreviewComp)) {
      icon = 0; // No icon for previews.
    } else if (ecs_world_has_t(world, e, SceneVfxDecalComp)) {
      icon = UiShape_Image;
    } else if (ecs_world_has_t(world, e, SceneVfxSystemComp)) {
      icon = UiShape_Grain;
    } else if (ecs_world_has_t(world, e, SceneLightPointComp)) {
      icon = UiShape_Light;
    } else if (ecs_world_has_t(world, e, SceneLightDirComp)) {
      icon = UiShape_Light;
    } else if (ecs_world_has_t(world, e, SceneLightAmbientComp)) {
      icon = UiShape_Light;
    } else if (ecs_world_has_t(world, e, SceneSoundComp)) {
      icon = UiShape_MusicNote;
    } else if (ecs_world_has_t(world, e, SceneRenderableComp)) {
      icon = UiShape_WebAsset;
    } else if (ecs_world_has_t(world, e, SceneCollisionComp)) {
      icon = UiShape_Dashboard;
    } else if (ecs_world_has_t(world, e, SceneCameraComp)) {
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
  ecs_register_view(CameraView);
  ecs_register_view(PrefabMapView);

  ecs_register_system(
      DebugInspectorUpdatePanelSys,
      ecs_view_id(GlobalPanelUpdateView),
      ecs_view_id(SettingsWriteView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView),
      ecs_view_id(PrefabMapView));

  ecs_register_system(
      DebugInspectorToolUpdateSys, ecs_view_id(GlobalToolUpdateView), ecs_view_id(SubjectView));

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
      .setNameBuffer = dynstring_create(g_allocHeap, 0));

  if (type == DebugPanelType_Detached) {
    ui_panel_maximize(&inspectorPanel->panel);
  }

  return panelEntity;
}
