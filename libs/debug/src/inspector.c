#include "core_array.h"
#include "core_bits.h"
#include "core_format.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "debug_gizmo.h"
#include "debug_inspector.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_stats.h"
#include "debug_text.h"
#include "ecs_world.h"
#include "input_manager.h"
#include "scene_bounds.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_nav.h"
#include "scene_renderable.h"
#include "scene_selection.h"
#include "scene_tag.h"
#include "scene_target.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "ui.h"

static const String g_tooltipReset = string_static("Reset the value to default.");

typedef enum {
  DebugInspectorTool_None,
  DebugInspectorTool_Translation,
  DebugInspectorTool_Rotation,
  DebugInspectorTool_Scale,

  DebugInspectorTool_Count,
} DebugInspectorTool;

typedef enum {
  DebugInspectorVis_Origin,
  DebugInspectorVis_Name,
  DebugInspectorVis_Locomotion,
  DebugInspectorVis_Collision,
  DebugInspectorVis_CollisionBounds,
  DebugInspectorVis_BoundsLocal,
  DebugInspectorVis_BoundsGlobal,
  DebugInspectorVis_NavigationPath,
  DebugInspectorVis_NavigationGrid,
  DebugInspectorVis_Target,

  DebugInspectorVis_Count,
} DebugInspectorVis;

typedef enum {
  DebugInspectorVisMode_SelectedOnly,
  DebugInspectorVisMode_All,

  DebugInspectorVisMode_Count,
} DebugInspectorVisMode;

static const String g_toolNames[] = {
    [DebugInspectorTool_None]        = string_static("None"),
    [DebugInspectorTool_Translation] = string_static("Translation"),
    [DebugInspectorTool_Rotation]    = string_static("Rotation"),
    [DebugInspectorTool_Scale]       = string_static("Scale"),
};
ASSERT(array_elems(g_toolNames) == DebugInspectorTool_Count, "Missing tool name");

static const String g_visNames[] = {
    [DebugInspectorVis_Origin]          = string_static("Origin"),
    [DebugInspectorVis_Name]            = string_static("Name"),
    [DebugInspectorVis_Locomotion]      = string_static("Locomotion"),
    [DebugInspectorVis_Collision]       = string_static("Collision"),
    [DebugInspectorVis_CollisionBounds] = string_static("CollisionBounds"),
    [DebugInspectorVis_BoundsLocal]     = string_static("BoundsLocal"),
    [DebugInspectorVis_BoundsGlobal]    = string_static("BoundsGlobal"),
    [DebugInspectorVis_NavigationPath]  = string_static("NavigationPath"),
    [DebugInspectorVis_NavigationGrid]  = string_static("NavigationGrid"),
    [DebugInspectorVis_Target]          = string_static("Target"),
};
ASSERT(array_elems(g_visNames) == DebugInspectorVis_Count, "Missing vis name");

static const String g_visModeNames[] = {
    [DebugInspectorVisMode_SelectedOnly] = string_static("SelectedOnly"),
    [DebugInspectorVisMode_All]          = string_static("All"),
};
ASSERT(array_elems(g_visModeNames) == DebugInspectorVisMode_Count, "Missing vis mode name");

ecs_comp_define(DebugInspectorSettingsComp) {
  DebugInspectorTool    tool;
  DebugInspectorVisMode visMode;
  u32                   visFlags;
};

ecs_comp_define(DebugInspectorPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  u32          totalRows;
  GeoVector    transformRotEulerDeg; // Local copy of rotation as euler angles to use while editing.
};

ecs_view_define(SettingsWriteView) { ecs_access_write(DebugInspectorSettingsComp); }

ecs_view_define(GlobalPanelUpdateView) {
  ecs_access_read(SceneSelectionComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(DebugStatsGlobalComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugInspectorPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(GlobalToolUpdateView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneSelectionComp);
  ecs_access_write(DebugGizmoComp);
  ecs_access_write(DebugInspectorSettingsComp);
  ecs_access_write(DebugStatsGlobalComp);
}

ecs_view_define(GlobalVisDrawView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneSelectionComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_write(DebugInspectorSettingsComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugStatsGlobalComp);
  ecs_access_write(DebugTextComp);
}

ecs_view_define(SubjectView) {
  ecs_access_maybe_read(SceneLocomotionComp);
  ecs_access_maybe_read(SceneNameComp);
  ecs_access_maybe_read(SceneNavAgentComp);
  ecs_access_maybe_read(SceneNavPathComp);
  ecs_access_maybe_read(SceneTargetFinderComp);
  ecs_access_maybe_read(SceneVelocityComp);
  ecs_access_maybe_write(SceneBoundsComp);
  ecs_access_maybe_write(SceneCollisionComp);
  ecs_access_maybe_write(SceneFactionComp);
  ecs_access_maybe_write(SceneHealthComp);
  ecs_access_maybe_write(SceneRenderableComp);
  ecs_access_maybe_write(SceneScaleComp);
  ecs_access_maybe_write(SceneTagComp);
  ecs_access_write(SceneTransformComp);
}

static void inspector_notify_tool(DebugInspectorSettingsComp* set, DebugStatsGlobalComp* stats) {
  debug_stats_notify(stats, string_lit("Tool"), g_toolNames[set->tool]);
}

static void inspector_notify_vis(
    DebugInspectorSettingsComp* set, DebugStatsGlobalComp* stats, const DebugInspectorVis vis) {
  debug_stats_notify(
      stats,
      fmt_write_scratch("Visualize {}", fmt_text(g_visNames[vis])),
      (set->visFlags & (1 << vis)) ? string_lit("enabled") : string_lit("disabled"));
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
      ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
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

static bool inspector_panel_draw_editor_f32(UiCanvasComp* canvas, f32* val) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .min = f32_min, .max = f32_max, .flags = UiWidget_DirtyWhileEditing)) {
    *val = (f32)v;
    return true;
  }
  return false;
}

MAYBE_UNUSED static bool inspector_panel_draw_editor_u32(UiCanvasComp* canvas, u32* val) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .max = u32_max, .step = 1, .flags = UiWidget_DirtyWhileEditing)) {
    *val = (u32)v;
    return true;
  }
  return false;
}

static bool
inspector_panel_draw_editor_vec(UiCanvasComp* canvas, GeoVector* val, const u8 numComps) {
  static const f32 g_spacing   = 10.0f;
  const u8         numSpacings = numComps - 1;
  const UiAlign    align       = UiAlign_MiddleLeft;
  ui_layout_push(canvas);
  ui_layout_resize(canvas, align, ui_vector(1.0f / numComps, 0), UiBase_Current, Ui_X);
  ui_layout_grow(
      canvas, align, ui_vector(numSpacings * -g_spacing / numComps, 0), UiBase_Absolute, Ui_X);

  bool isDirty = false;
  for (u8 comp = 0; comp != numComps; ++comp) {
    isDirty |= inspector_panel_draw_editor_f32(canvas, &val->comps[comp]);
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
  ui_layout_pop(canvas);
  return isDirty;
}

static bool inspector_panel_draw_editor_vec_resettable(
    UiCanvasComp* canvas, GeoVector* val, const u8 numComps) {
  ui_layout_push(canvas);
  ui_layout_grow(canvas, UiAlign_MiddleLeft, ui_vector(-30, 0), UiBase_Absolute, Ui_X);
  bool isDirty = inspector_panel_draw_editor_vec(canvas, val, numComps);
  ui_layout_next(canvas, Ui_Right, 8);
  ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(22, 0), UiBase_Absolute, Ui_X);
  if (ui_button(canvas, .label = ui_shape_scratch(UiShape_Default), .tooltip = g_tooltipReset)) {
    for (u8 comp = 0; comp != numComps; ++comp) {
      val->comps[comp] = 0;
    }
    isDirty = true;
  }
  ui_layout_pop(canvas);
  return isDirty;
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
    if (inspector_panel_draw_editor_vec_resettable(canvas, &transform->position, 3)) {
      // Clamp the position to a sane value.
      transform->position = geo_vector_clamp(transform->position, 1e3f);
    }

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Rotation"));
    ui_table_next_column(canvas, table);
    if (inspector_panel_draw_editor_vec_resettable(canvas, &panelComp->transformRotEulerDeg, 3)) {
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
    if (inspector_panel_draw_editor_f32(canvas, &scale->scale)) {
      // Clamp the scale to a sane value.
      scale->scale = math_clamp_f32(scale->scale, 1e-2f, 1e2f);
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
      inspector_panel_draw_editor_f32(canvas, &health->max);
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
      inspector_panel_draw_value_string(canvas, scene_faction_name(faction->id));
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
      u32       flags    = finder->flags;
      GeoVector tgtPos   = finder->targetPosition;
      f32       tgtScore = math_sqrt_f32(finder->targetScoreSqr);
      f32       tgtDist  = finder->targetDistance;

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Entity"));
      ui_table_next_column(canvas, table);
      inspector_panel_draw_value_entity(canvas, finder->target);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Overriden"));
      ui_table_next_column(canvas, table);
      ui_toggle_flag(canvas, &flags, SceneTarget_Overriden);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Position"));
      ui_table_next_column(canvas, table);
      inspector_panel_draw_editor_vec(canvas, &tgtPos, 3);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Distance"));
      ui_table_next_column(canvas, table);
      inspector_panel_draw_editor_f32(canvas, &tgtDist);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Score"));
      ui_table_next_column(canvas, table);
      inspector_panel_draw_editor_f32(canvas, &tgtScore);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Line of Sight"));
      ui_table_next_column(canvas, table);
      ui_toggle_flag(canvas, &flags, SceneTarget_LineOfSight);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Time until refresh"));
      ui_table_next_column(canvas, table);
      ui_label(canvas, fmt_write_scratch("{}", fmt_duration(finder->nextRefreshTime - time->time)));
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
  SceneCollisionComp* collision = subject ? ecs_view_write_t(subject, SceneCollisionComp) : null;
  if (collision) {
    inspector_panel_next(canvas, panelComp, table);
    if (inspector_panel_section(canvas, string_lit("Collision"))) {

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Type"));
      ui_table_next_column(canvas, table);
      inspector_panel_draw_value_string(canvas, scene_collision_type_name(collision->type));

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Layer"));
      ui_table_next_column(canvas, table);
      if (bits_popcnt((u32)collision->layer) == 1) {
        inspector_panel_draw_value_string(canvas, scene_layer_name(collision->layer));
      } else {
        inspector_panel_draw_value_string(canvas, string_lit("< Multiple >"));
      }

      switch (collision->type) {
      case SceneCollisionType_Sphere: {
        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Offset"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_vec(canvas, &collision->sphere.offset, 3);

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Radius"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_f32(canvas, &collision->sphere.radius);
      } break;
      case SceneCollisionType_Capsule: {
        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Offset"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_vec(canvas, &collision->capsule.offset, 3);

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Direction"));
        ui_table_next_column(canvas, table);
        static const String g_collisionDirNames[] = {
            string_static("Up"), string_static("Forward"), string_static("Right")};
        ui_select(canvas, (i32*)&collision->capsule.dir, g_collisionDirNames, 3);

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Radius"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_f32(canvas, &collision->capsule.radius);

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Height"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_f32(canvas, &collision->capsule.height);
      } break;
      case SceneCollisionType_Box: {
        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Min"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_vec(canvas, &collision->box.min, 3);

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Max"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_vec(canvas, &collision->box.max, 3);
      } break;
      case SceneCollisionType_Count:
        UNREACHABLE
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
      dirty |= inspector_panel_draw_editor_vec(canvas, &center, 3);

      inspector_panel_next(canvas, panelComp, table);
      ui_label(canvas, string_lit("Size"));
      ui_table_next_column(canvas, table);
      dirty |= inspector_panel_draw_editor_vec(canvas, &size, 3);

      if (dirty) {
        boundsComp->local = geo_box_from_center(center, size);
      }
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
    ui_label(canvas, string_lit("Tool"));
    ui_table_next_column(canvas, table);
    if (ui_select(canvas, (i32*)&settings->tool, g_toolNames, array_elems(g_toolNames))) {
      inspector_notify_tool(settings, stats);
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
    UiCanvasComp*               canvas,
    DebugInspectorPanelComp*    panelComp,
    DebugInspectorSettingsComp* settings,
    EcsIterator*                subject) {
  const String title = fmt_write_scratch("{} Inspector Panel", fmt_ui_shape(ViewInAr));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 215);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const f32 totalHeight = ui_table_height(&table, panelComp->totalRows);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);
  panelComp->totalRows = 0;

  /**
   * NOTE: The sections draw a variable amount of elements, thus we jump to the next id block
   * afterwards to keep consistent ids.
   */

  inspector_panel_draw_entity_info(world, canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_transform(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_health(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_faction(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_target(time, canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_renderable(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_tags(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas);

  inspector_panel_draw_collision(canvas, panelComp, &table, subject);
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
  EcsView*     view = ecs_world_view_t(world, SettingsWriteView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  return itr ? ecs_view_write_t(itr, DebugInspectorSettingsComp)
             : ecs_world_add_t(world, ecs_world_global(world), DebugInspectorSettingsComp);
}

ecs_system_define(DebugInspectorUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalPanelUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneSelectionComp*   selection = ecs_view_read_t(globalItr, SceneSelectionComp);
  const SceneTimeComp*        time      = ecs_view_read_t(globalItr, SceneTimeComp);
  DebugInspectorSettingsComp* settings  = inspector_settings_get_or_create(world);
  DebugStatsGlobalComp*       stats     = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  EcsView*     subjectView = ecs_world_view_t(world, SubjectView);
  EcsIterator* subjectItr  = ecs_view_maybe_at(subjectView, scene_selection_main(selection));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId        entity    = ecs_view_entity(itr);
    DebugInspectorPanelComp* panelComp = ecs_view_write_t(itr, DebugInspectorPanelComp);
    UiCanvasComp*            canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    inspector_panel_draw(world, stats, time, canvas, panelComp, settings, subjectItr);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

static void
debug_inspector_toggle_tool(DebugInspectorSettingsComp* set, const DebugInspectorTool tool) {
  if (set->tool != tool) {
    set->tool = tool;
  } else {
    set->tool = DebugInspectorTool_None;
  }
}

ecs_system_define(DebugInspectorToolUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalToolUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const InputManagerComp*     input = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneSelectionComp*   sel   = ecs_view_read_t(globalItr, SceneSelectionComp);
  DebugGizmoComp*             gizmo = ecs_view_write_t(globalItr, DebugGizmoComp);
  DebugInspectorSettingsComp* set   = ecs_view_write_t(globalItr, DebugInspectorSettingsComp);
  DebugStatsGlobalComp*       stats = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  if (input_triggered_lit(input, "DebugInspectorToolTranslation")) {
    debug_inspector_toggle_tool(set, DebugInspectorTool_Translation);
    inspector_notify_tool(set, stats);
  }
  if (input_triggered_lit(input, "DebugInspectorToolRotation")) {
    debug_inspector_toggle_tool(set, DebugInspectorTool_Rotation);
    inspector_notify_tool(set, stats);
  }
  if (input_triggered_lit(input, "DebugInspectorToolScale")) {
    debug_inspector_toggle_tool(set, DebugInspectorTool_Scale);
    inspector_notify_tool(set, stats);
  }

  EcsIterator* subjectItr = ecs_view_itr(ecs_world_view_t(world, SubjectView));
  for (const EcsEntityId* e = scene_selection_begin(sel); e != scene_selection_end(sel); ++e) {
    if (ecs_view_maybe_jump(subjectItr, *e)) {
      const DebugGizmoId  gizmoId   = (DebugGizmoId)ecs_view_entity(subjectItr);
      SceneTransformComp* transform = ecs_view_write_t(subjectItr, SceneTransformComp);
      SceneScaleComp*     scaleComp = ecs_view_write_t(subjectItr, SceneScaleComp);
      switch (set->tool) {
      case DebugInspectorTool_Translation:
        if (transform) {
          debug_gizmo_translation(gizmo, gizmoId, &transform->position, transform->rotation);
        }
        break;
      case DebugInspectorTool_Rotation:
        if (transform) {
          debug_gizmo_rotation(gizmo, gizmoId, transform->position, &transform->rotation);
        }
        break;
      case DebugInspectorTool_Scale:
        if (scaleComp) {
          const GeoVector position = transform ? transform->position : geo_vector(0);
          debug_gizmo_scale_uniform(gizmo, gizmoId, position, &scaleComp->scale);
        }
        break;
      case DebugInspectorTool_None:
      case DebugInspectorTool_Count:
        break;
      }
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

  const f32 separationStrength = math_clamp_f32(geo_vector_mag(loco->lastSeparation) * 100, 0, 1);

  const GeoColor circleColor = geo_color_lerp(geo_color_white, geo_color_red, separationStrength);
  debug_circle(shape, pos, geo_quat_up_to_forward, loco->radius * scaleVal, circleColor);

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
  static const GeoColor g_colorFill = {1, 0, 0, 0.2f};
  static const GeoColor g_colorWire = {1, 0, 0, 1};

  switch (collision->type) {
  case SceneCollisionType_Sphere: {
    const GeoSphere c = scene_collision_world_sphere(&collision->sphere, transform, scale);
    debug_sphere(shape, c.point, c.radius, g_colorFill, DebugShape_Fill);
    debug_sphere(shape, c.point, c.radius, g_colorWire, DebugShape_Wire);
  } break;
  case SceneCollisionType_Capsule: {
    const GeoCapsule c = scene_collision_world_capsule(&collision->capsule, transform, scale);
    debug_capsule(shape, c.line.a, c.line.b, c.radius, g_colorFill, DebugShape_Fill);
    debug_capsule(shape, c.line.a, c.line.b, c.radius, g_colorWire, DebugShape_Wire);
  } break;
  case SceneCollisionType_Box: {
    const GeoBoxRotated b      = scene_collision_world_box(&collision->box, transform, scale);
    const GeoVector     center = geo_box_center(&b.box);
    const GeoVector     size   = geo_box_size(&b.box);
    debug_box(shape, center, b.rotation, size, g_colorFill, DebugShape_Fill);
    debug_box(shape, center, b.rotation, size, g_colorWire, DebugShape_Wire);
  } break;
  case SceneCollisionType_Count:
    UNREACHABLE
  }
}

static void inspector_vis_draw_collision_bounds(
    DebugShapeComp*           shape,
    const SceneCollisionComp* collision,
    const SceneTransformComp* transform,
    const SceneScaleComp*     scale) {
  const GeoBox    b      = scene_collision_world_bounds(collision, transform, scale);
  const GeoVector center = geo_box_center(&b);
  const GeoVector size   = geo_box_size(&b);
  debug_box(shape, center, geo_quat_ident, size, geo_color(1, 0, 1, 0.2f), DebugShape_Fill);
  debug_box(shape, center, geo_quat_ident, size, geo_color(1, 0, 1, 0.5f), DebugShape_Wire);
}

static void inspector_vis_draw_bounds_local(
    DebugShapeComp*           shape,
    const SceneBoundsComp*    bounds,
    const SceneTransformComp* transform,
    const SceneScaleComp*     scale) {
  const GeoBoxRotated b      = scene_bounds_world_rotated(bounds, transform, scale);
  const GeoVector     center = geo_box_center(&b.box);
  const GeoVector     size   = geo_box_size(&b.box);
  debug_box(shape, center, b.rotation, size, geo_color(0, 1, 0, 0.2f), DebugShape_Fill);
  debug_box(shape, center, b.rotation, size, geo_color(0, 1, 0, 0.5f), DebugShape_Wire);
}

static void inspector_vis_draw_bounds_global(
    DebugShapeComp*           shape,
    const SceneBoundsComp*    bounds,
    const SceneTransformComp* transform,
    const SceneScaleComp*     scale) {
  const GeoBox    b      = scene_bounds_world(bounds, transform, scale);
  const GeoVector center = geo_box_center(&b);
  const GeoVector size   = geo_box_size(&b);
  debug_box(shape, center, geo_quat_ident, size, geo_color(0, 0, 1, 0.2f), DebugShape_Fill);
  debug_box(shape, center, geo_quat_ident, size, geo_color(0, 0, 1, 0.5f), DebugShape_Wire);
}

static void inspector_vis_draw_navigation_path(
    DebugShapeComp*          shape,
    const SceneNavEnvComp*   nav,
    const SceneNavAgentComp* agent,
    const SceneNavPathComp*  path) {
  for (u32 i = 1; i < path->cellCount; ++i) {
    const GeoVector posA = scene_nav_position(nav, path->cells[i - 1]);
    const GeoVector posB = scene_nav_position(nav, path->cells[i]);
    debug_line(shape, posA, posB, geo_color_white);
  }
  if (agent->flags & SceneNavAgent_Traveling) {
    debug_sphere(shape, agent->target, 0.1f, geo_color_blue, DebugShape_Overlay);
  }
}

static void
inspector_vis_draw_target(DebugShapeComp* shape, const SceneTargetFinderComp* targetFinder) {
  if (targetFinder->target) {
    debug_sphere(shape, targetFinder->targetPosition, 0.1f, geo_color_lime, DebugShape_Overlay);
  }
}

static void inspector_vis_draw_subject(
    DebugShapeComp*                   shape,
    DebugTextComp*                    text,
    const DebugInspectorSettingsComp* set,
    const SceneNavEnvComp*            nav,
    EcsIterator*                      subject) {
  const SceneBoundsComp*       boundsComp       = ecs_view_read_t(subject, SceneBoundsComp);
  const SceneCollisionComp*    collisionComp    = ecs_view_read_t(subject, SceneCollisionComp);
  const SceneLocomotionComp*   locoComp         = ecs_view_read_t(subject, SceneLocomotionComp);
  const SceneNameComp*         nameComp         = ecs_view_read_t(subject, SceneNameComp);
  const SceneNavAgentComp*     navAgentComp     = ecs_view_read_t(subject, SceneNavAgentComp);
  const SceneNavPathComp*      navPathComp      = ecs_view_read_t(subject, SceneNavPathComp);
  const SceneScaleComp*        scaleComp        = ecs_view_read_t(subject, SceneScaleComp);
  const SceneTargetFinderComp* targetFinderComp = ecs_view_read_t(subject, SceneTargetFinderComp);
  const SceneTransformComp*    transformComp    = ecs_view_read_t(subject, SceneTransformComp);
  const SceneVelocityComp*     veloComp         = ecs_view_read_t(subject, SceneVelocityComp);

  if (transformComp && set->visFlags & (1 << DebugInspectorVis_Origin)) {
    debug_sphere(shape, transformComp->position, 0.05f, geo_color_fuchsia, DebugShape_Overlay);
    if (veloComp && geo_vector_mag(veloComp->velocityAvg) > 1e-3f) {
      const GeoVector posOneSecAway = scene_position_predict(transformComp, veloComp, time_second);
      debug_arrow(shape, transformComp->position, posOneSecAway, 0.15f, geo_color_green);
    }
  }
  if (nameComp && set->visFlags & (1 << DebugInspectorVis_Name)) {
    const String    name = stringtable_lookup(g_stringtable, nameComp->name);
    const GeoVector pos  = geo_vector_add(transformComp->position, geo_vector_mul(geo_up, 0.1f));
    debug_text(text, pos, name, geo_color_white);
  }
  if (locoComp && set->visFlags & (1 << DebugInspectorVis_Locomotion)) {
    inspector_vis_draw_locomotion(shape, locoComp, transformComp, scaleComp);
  }
  if (collisionComp && set->visFlags & (1 << DebugInspectorVis_Collision)) {
    inspector_vis_draw_collision(shape, collisionComp, transformComp, scaleComp);
  }
  if (collisionComp && set->visFlags & (1 << DebugInspectorVis_CollisionBounds)) {
    inspector_vis_draw_collision_bounds(shape, collisionComp, transformComp, scaleComp);
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
    inspector_vis_draw_navigation_path(shape, nav, navAgentComp, navPathComp);
  }
  if (targetFinderComp && set->visFlags & (1 << DebugInspectorVis_Target)) {
    inspector_vis_draw_target(shape, targetFinderComp);
  }
}

static void inspector_vis_draw_navigation_grid(
    DebugShapeComp* shape, DebugTextComp* text, const SceneNavEnvComp* nav) {

  DynString textBuffer = dynstring_create_over(mem_stack(64));

  const GeoNavRegion   bounds    = scene_nav_bounds(nav);
  const GeoVector      cellSize  = scene_nav_cell_size(nav);
  const DebugShapeMode shapeMode = DebugShape_Overlay;
  for (u32 y = bounds.min.y; y != bounds.max.y; ++y) {
    for (u32 x = bounds.min.x; x != bounds.max.x; ++x) {
      const GeoNavCell   cell      = {.x = x, .y = y};
      const GeoVector    pos       = scene_nav_position(nav, (GeoNavCell){.x = x, .y = y});
      const bool         highlight = (x & 1) == (y & 1);
      const bool         blocked   = scene_nav_blocked(nav, cell);
      const GeoNavIsland island    = scene_nav_island(nav, cell);

      GeoColor color;
      if (blocked) {
        color = geo_color(1, 0, 0, highlight ? 0.5f : 0.3f);
      } else if (scene_nav_occupied_moving(nav, cell)) {
        color = geo_color(1, 0, 1, highlight ? 0.3f : 0.2f);
      } else if (scene_nav_occupied(nav, cell)) {
        color = geo_color(0, 0, 1, highlight ? 0.2f : 0.1f);
      } else if (island == 1) {
        continue; // Skip drawing unblocked and occupied cells on the main island.
      } else {
        color = geo_color(0, 1, 0, highlight ? 0.2f : 0.1f);
      }
      debug_quad(shape, pos, geo_quat_up_to_forward, cellSize.x, cellSize.z, color, shapeMode);

      if (!blocked) {
        dynstring_clear(&textBuffer);
        format_write_u64(&textBuffer, island, &format_opts_int());
        debug_text(text, pos, dynstring_view(&textBuffer), geo_color_white);
      }
    }
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

  static const String g_drawHotkeys[DebugInspectorVis_Count] = {
      [DebugInspectorVis_Collision]      = string_static("DebugInspectorVisCollision"),
      [DebugInspectorVis_Locomotion]     = string_static("DebugInspectorVisLocomotion"),
      [DebugInspectorVis_NavigationPath] = string_static("DebugInspectorVisNavigationPath"),
      [DebugInspectorVis_NavigationGrid] = string_static("DebugInspectorVisNavigationGrid"),
      [DebugInspectorVis_Target]         = string_static("DebugInspectorVisTarget"),
  };
  for (DebugInspectorVis vis = 0; vis != DebugInspectorVis_Count; ++vis) {
    const u32 hotKeyHash = string_hash(g_drawHotkeys[vis]);
    if (hotKeyHash && input_triggered_hash(input, hotKeyHash)) {
      set->visFlags ^= (1 << vis);
      inspector_notify_vis(set, stats, vis);
    }
  }

  if (!set->visFlags) {
    return;
  }
  const SceneNavEnvComp*    nav   = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneSelectionComp* sel   = ecs_view_read_t(globalItr, SceneSelectionComp);
  DebugShapeComp*           shape = ecs_view_write_t(globalItr, DebugShapeComp);
  DebugTextComp*            text  = ecs_view_write_t(globalItr, DebugTextComp);

  EcsView* subjectView = ecs_world_view_t(world, SubjectView);
  switch (set->visMode) {
  case DebugInspectorVisMode_SelectedOnly: {
    EcsIterator* subjectItr = ecs_view_itr(subjectView);
    for (const EcsEntityId* e = scene_selection_begin(sel); e != scene_selection_end(sel); ++e) {
      if (ecs_view_maybe_jump(subjectItr, *e)) {
        inspector_vis_draw_subject(shape, text, set, nav, subjectItr);
      }
    }
  } break;
  case DebugInspectorVisMode_All: {
    for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
      inspector_vis_draw_subject(shape, text, set, nav, itr);
    }
  } break;
  case DebugInspectorVisMode_Count:
    UNREACHABLE
  }

  if (set->visFlags & (1 << DebugInspectorVis_NavigationGrid)) {
    inspector_vis_draw_navigation_grid(shape, text, nav);
  }
}

ecs_module_init(debug_inspector_module) {
  ecs_register_comp(DebugInspectorSettingsComp);
  ecs_register_comp(DebugInspectorPanelComp);

  ecs_register_view(SettingsWriteView);
  ecs_register_view(GlobalPanelUpdateView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(GlobalToolUpdateView);
  ecs_register_view(GlobalVisDrawView);
  ecs_register_view(SubjectView);

  ecs_register_system(
      DebugInspectorUpdatePanelSys,
      ecs_view_id(GlobalPanelUpdateView),
      ecs_view_id(SettingsWriteView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView));

  ecs_register_system(
      DebugInspectorToolUpdateSys, ecs_view_id(GlobalToolUpdateView), ecs_view_id(SubjectView));

  ecs_register_system(
      DebugInspectorVisDrawSys, ecs_view_id(GlobalVisDrawView), ecs_view_id(SubjectView));

  ecs_order(DebugInspectorToolUpdateSys, DebugOrder_InspectorToolUpdate);
  ecs_order(DebugInspectorVisDrawSys, DebugOrder_InspectorDebugDraw);
}

EcsEntityId debug_inspector_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugInspectorPanelComp,
      .panel = ui_panel(.position = ui_vector(0.2f, 0.5f), .size = ui_vector(500, 550)));
  return panelEntity;
}
