#include "core_array.h"
#include "core_bits.h"
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
#include "scene_name.h"
#include "scene_renderable.h"
#include "scene_selection.h"
#include "scene_tag.h"
#include "scene_transform.h"
#include "ui.h"

static const String g_tooltipReset = string_static("Reset the value to default.");

typedef enum {
  DebugInspectorTool_None,
  DebugInspectorTool_Translation,
  DebugInspectorTool_Rotation,

  DebugInspectorTool_Count,
} DebugInspectorTool;

typedef enum {
  DebugInspectorFlags_DrawName         = 1 << 0,
  DebugInspectorFlags_DrawCollision    = 1 << 1,
  DebugInspectorFlags_DrawBoundsLocal  = 1 << 2,
  DebugInspectorFlags_DrawBoundsGlobal = 1 << 3,

  DebugInspectorFlags_DrawAny = DebugInspectorFlags_DrawName | DebugInspectorFlags_DrawCollision |
                                DebugInspectorFlags_DrawBoundsLocal |
                                DebugInspectorFlags_DrawBoundsGlobal

} DebugInspectorFlags;

typedef enum {
  DebugInspectorDraw_SelectedOnly,
  DebugInspectorDraw_All,

  DebugInspectorDrawMode_Count,
} DebugInspectorDrawMode;

static const String g_toolNames[] = {
    [DebugInspectorTool_None]        = string_static("None"),
    [DebugInspectorTool_Translation] = string_static("Translation"),
    [DebugInspectorTool_Rotation]    = string_static("Rotation"),
};
ASSERT(array_elems(g_toolNames) == DebugInspectorTool_Count, "Missing tool name");

static const String g_drawModeNames[] = {
    [DebugInspectorDraw_SelectedOnly] = string_static("SelectedOnly"),
    [DebugInspectorDraw_All]          = string_static("All"),
};
ASSERT(array_elems(g_drawModeNames) == DebugInspectorDrawMode_Count, "Missing draw-mode name");

ecs_comp_define(DebugInspectorSettingsComp) {
  DebugInspectorTool     tool;
  DebugInspectorDrawMode mode;
  DebugInspectorFlags    flags;
};

ecs_comp_define(DebugInspectorPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  u32          totalRows;
  GeoVector    transformRotEulerDeg; // Local copy of rotation as euler angles to use while editing.
};

ecs_view_define(SettingsWriteView) { ecs_access_write(DebugInspectorSettingsComp); }
ecs_view_define(GlobalPanelUpdateView) { ecs_access_read(SceneSelectionComp); }

ecs_view_define(GlobalShapeDrawView) {
  ecs_access_read(DebugInspectorSettingsComp);
  ecs_access_read(SceneSelectionComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugTextComp);
}

ecs_view_define(GlobalToolUpdateView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneSelectionComp);
  ecs_access_write(DebugGizmoComp);
  ecs_access_write(DebugInspectorSettingsComp);
  ecs_access_write(DebugStatsGlobalComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugInspectorPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(SubjectView) {
  ecs_access_maybe_read(SceneNameComp);
  ecs_access_maybe_write(SceneBoundsComp);
  ecs_access_maybe_write(SceneCollisionComp);
  ecs_access_maybe_write(SceneScaleComp);
  ecs_access_maybe_write(SceneTagComp);
  ecs_access_write(SceneRenderableComp);
  ecs_access_write(SceneTransformComp);
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

static bool inspector_panel_draw_editor_float(UiCanvasComp* canvas, f32* val) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .min = f32_min, .max = f32_max, .flags = UiWidget_DirtyWhileEditing)) {
    *val = (f32)v;
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
    isDirty |= inspector_panel_draw_editor_float(canvas, &val->comps[comp]);
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
    inspector_panel_draw_editor_float(canvas, &scale->scale);
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
      switch (collision->type) {
      case SceneCollisionType_Sphere: {
        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Type"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_value_string(canvas, string_lit("Sphere"));

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Offset"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_vec(canvas, &collision->sphere.offset, 3);

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Radius"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_float(canvas, &collision->sphere.radius);
      } break;
      case SceneCollisionType_Capsule: {
        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Type"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_value_string(canvas, string_lit("Capsule"));

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
        inspector_panel_draw_editor_float(canvas, &collision->capsule.radius);

        inspector_panel_next(canvas, panelComp, table);
        ui_label(canvas, string_lit("Height"));
        ui_table_next_column(canvas, table);
        inspector_panel_draw_editor_float(canvas, &collision->capsule.height);
      } break;
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
    DebugInspectorPanelComp*    panelComp,
    UiTable*                    table,
    DebugInspectorSettingsComp* settings) {
  inspector_panel_next(canvas, panelComp, table);
  if (inspector_panel_section(canvas, string_lit("Settings"))) {
    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Tool"));
    ui_table_next_column(canvas, table);
    ui_select(canvas, (i32*)&settings->tool, g_toolNames, array_elems(g_toolNames));

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Draw mode"));
    ui_table_next_column(canvas, table);
    ui_select(canvas, (i32*)&settings->mode, g_drawModeNames, array_elems(g_drawModeNames));

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Draw name"));
    ui_table_next_column(canvas, table);
    ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawName);

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Draw collision"));
    ui_table_next_column(canvas, table);
    ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawCollision);

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Draw bounds local"));
    ui_table_next_column(canvas, table);
    ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawBoundsLocal);

    inspector_panel_next(canvas, panelComp, table);
    ui_label(canvas, string_lit("Draw bounds global"));
    ui_table_next_column(canvas, table);
    ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawBoundsGlobal);
  }
}

static void inspector_panel_draw(
    EcsWorld*                   world,
    UiCanvasComp*               canvas,
    DebugInspectorPanelComp*    panelComp,
    DebugInspectorSettingsComp* settings,
    EcsIterator*                subject) {
  const String title = fmt_write_scratch("{} Inspector Panel", fmt_ui_shape(ViewInAr));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  const f32 totalHeight = ui_table_height(&table, panelComp->totalRows);
  ui_scrollview_begin(canvas, &panelComp->scrollview, totalHeight);
  panelComp->totalRows = 0;

  inspector_panel_draw_entity_info(world, canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_transform(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_renderable(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_tags(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_collision(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_bounds(canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_components(world, canvas, panelComp, &table, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_settings(canvas, panelComp, &table, settings);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

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
  DebugInspectorSettingsComp* settings  = inspector_settings_get_or_create(world);

  EcsView*     subjectView = ecs_world_view_t(world, SubjectView);
  EcsIterator* subjectItr  = ecs_view_maybe_at(subjectView, scene_selected(selection));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId        entity    = ecs_view_entity(itr);
    DebugInspectorPanelComp* panelComp = ecs_view_write_t(itr, DebugInspectorPanelComp);
    UiCanvasComp*            canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    inspector_panel_draw(world, canvas, panelComp, settings, subjectItr);

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
  const InputManagerComp*     input     = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneSelectionComp*   selection = ecs_view_read_t(globalItr, SceneSelectionComp);
  DebugGizmoComp*             gizmo     = ecs_view_write_t(globalItr, DebugGizmoComp);
  DebugInspectorSettingsComp* set       = ecs_view_write_t(globalItr, DebugInspectorSettingsComp);
  DebugStatsGlobalComp*       stats     = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  if (input_triggered_lit(input, "DebugInspectorToolTranslation")) {
    debug_inspector_toggle_tool(set, DebugInspectorTool_Translation);
    debug_stats_notify(stats, string_lit("Tool"), g_toolNames[set->tool]);
  }
  if (input_triggered_lit(input, "DebugInspectorToolRotation")) {
    debug_inspector_toggle_tool(set, DebugInspectorTool_Rotation);
    debug_stats_notify(stats, string_lit("Tool"), g_toolNames[set->tool]);
  }

  EcsView*     subjectView = ecs_world_view_t(world, SubjectView);
  EcsIterator* subjectItr  = ecs_view_maybe_at(subjectView, scene_selected(selection));
  if (subjectItr) {
    const DebugGizmoId  gizmoId       = (DebugGizmoId)ecs_view_entity(subjectItr);
    SceneTransformComp* transformComp = ecs_view_write_t(subjectItr, SceneTransformComp);
    switch (set->tool) {
    case DebugInspectorTool_Translation:
      debug_gizmo_translation(gizmo, gizmoId, &transformComp->position, transformComp->rotation);
      break;
    case DebugInspectorTool_Rotation:
      debug_gizmo_rotation(gizmo, gizmoId, transformComp->position, &transformComp->rotation);
      break;
    case DebugInspectorTool_None:
    case DebugInspectorTool_Count:
      break;
    }
  }
}

static void inspector_shape_draw_collision(
    DebugShapeComp*           shape,
    const SceneCollisionComp* collision,
    const SceneTransformComp* transform,
    const SceneScaleComp*     scale) {
  switch (collision->type) {
  case SceneCollisionType_Sphere: {
    const GeoSphere c         = scene_collision_world_sphere(&collision->sphere, transform, scale);
    const GeoColor  colorFill = geo_color(1, 0, 0, 0.2f);
    const GeoColor  colorWire = geo_color(1, 0, 0, 1);
    debug_sphere(shape, c.point, c.radius, colorFill, DebugShape_Fill);
    debug_sphere(shape, c.point, c.radius, colorWire, DebugShape_Wire);
  } break;
  case SceneCollisionType_Capsule: {
    const GeoCapsule c = scene_collision_world_capsule(&collision->capsule, transform, scale);
    const GeoColor   colorFill = geo_color(1, 0, 0, 0.2f);
    const GeoColor   colorWire = geo_color(1, 0, 0, 1);
    debug_capsule(shape, c.line.a, c.line.b, c.radius, colorFill, DebugShape_Fill);
    debug_capsule(shape, c.line.a, c.line.b, c.radius, colorWire, DebugShape_Wire);
  } break;
  }
}

static void inspector_shape_draw_bounds_local(
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

static void inspector_shape_draw_bounds_global(
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

static void inspector_shape_draw_subject(
    DebugShapeComp*                   shape,
    DebugTextComp*                    text,
    const DebugInspectorSettingsComp* set,
    EcsIterator*                      subject) {
  SceneTransformComp*       transformComp = ecs_view_write_t(subject, SceneTransformComp);
  const SceneNameComp*      nameComp      = ecs_view_read_t(subject, SceneNameComp);
  const SceneBoundsComp*    boundsComp    = ecs_view_read_t(subject, SceneBoundsComp);
  const SceneCollisionComp* collisionComp = ecs_view_read_t(subject, SceneCollisionComp);
  const SceneScaleComp*     scaleComp     = ecs_view_read_t(subject, SceneScaleComp);

  if (nameComp && set->flags & DebugInspectorFlags_DrawName) {
    const String    name = stringtable_lookup(g_stringtable, nameComp->name);
    const GeoVector pos  = geo_vector_add(transformComp->position, geo_vector_mul(geo_up, 0.1f));
    debug_text(text, pos, name, geo_color_white);
  }
  if (collisionComp && set->flags & DebugInspectorFlags_DrawCollision) {
    inspector_shape_draw_collision(shape, collisionComp, transformComp, scaleComp);
  }
  if (boundsComp && !geo_box_is_inverted3(&boundsComp->local)) {
    if (set->flags & DebugInspectorFlags_DrawBoundsLocal) {
      inspector_shape_draw_bounds_local(shape, boundsComp, transformComp, scaleComp);
    }
    if (set->flags & DebugInspectorFlags_DrawBoundsGlobal) {
      inspector_shape_draw_bounds_global(shape, boundsComp, transformComp, scaleComp);
    }
  }
}

ecs_system_define(DebugInspectorShapeDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalShapeDrawView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const DebugInspectorSettingsComp* set = ecs_view_read_t(globalItr, DebugInspectorSettingsComp);
  if (!(set->flags & DebugInspectorFlags_DrawAny)) {
    return;
  }
  const SceneSelectionComp* selection = ecs_view_read_t(globalItr, SceneSelectionComp);
  DebugShapeComp*           shape     = ecs_view_write_t(globalItr, DebugShapeComp);
  DebugTextComp*            text      = ecs_view_write_t(globalItr, DebugTextComp);

  EcsView* subjectView = ecs_world_view_t(world, SubjectView);
  switch (set->mode) {
  case DebugInspectorDraw_SelectedOnly: {
    EcsIterator* subjectItr = ecs_view_maybe_at(subjectView, scene_selected(selection));
    if (subjectItr) {
      inspector_shape_draw_subject(shape, text, set, subjectItr);
    }
  } break;
  case DebugInspectorDraw_All: {
    for (EcsIterator* itr = ecs_view_itr(subjectView); ecs_view_walk(itr);) {
      inspector_shape_draw_subject(shape, text, set, itr);
    }
  } break;
  case DebugInspectorDrawMode_Count:
    UNREACHABLE
  }
}

ecs_module_init(debug_inspector_module) {
  ecs_register_comp(DebugInspectorSettingsComp);
  ecs_register_comp(DebugInspectorPanelComp);

  ecs_register_view(SettingsWriteView);
  ecs_register_view(GlobalPanelUpdateView);
  ecs_register_view(GlobalShapeDrawView);
  ecs_register_view(GlobalToolUpdateView);
  ecs_register_view(PanelUpdateView);
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
      DebugInspectorShapeDrawSys, ecs_view_id(GlobalShapeDrawView), ecs_view_id(SubjectView));

  ecs_order(DebugInspectorShapeDrawSys, DebugOrder_InspectorDebugDraw);
}

EcsEntityId debug_inspector_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugInspectorPanelComp, .panel = ui_panel(ui_vector(500, 400)));
  return panelEntity;
}
