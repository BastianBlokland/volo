#include "asset_manager.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "debug_inspector.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "debug_text.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_bounds.h"
#include "scene_collision.h"
#include "scene_name.h"
#include "scene_renderable.h"
#include "scene_selection.h"
#include "scene_transform.h"
#include "ui.h"

typedef enum {
  DebugInspectorFlags_DrawPivot        = 1 << 0,
  DebugInspectorFlags_DrawOrientation  = 1 << 1,
  DebugInspectorFlags_DrawName         = 1 << 2,
  DebugInspectorFlags_DrawCollision    = 1 << 3,
  DebugInspectorFlags_DrawBoundsLocal  = 1 << 4,
  DebugInspectorFlags_DrawBoundsGlobal = 1 << 5,

  DebugInspectorFlags_DrawAny =
      DebugInspectorFlags_DrawPivot | DebugInspectorFlags_DrawOrientation |
      DebugInspectorFlags_DrawName | DebugInspectorFlags_DrawCollision |
      DebugInspectorFlags_DrawBoundsLocal | DebugInspectorFlags_DrawBoundsGlobal
} DebugInspectorFlags;

ecs_comp_define(DebugInspectorSettingsComp) { DebugInspectorFlags flags; };

ecs_comp_define(DebugInspectorPanelComp) {
  UiPanel   panel;
  GeoVector transformRotEulerDeg; // Local copy of rotation as euler angles to use while editing.
};

ecs_view_define(SettingsWriteView) { ecs_access_write(DebugInspectorSettingsComp); }
ecs_view_define(GlobalUpdateView) { ecs_access_read(SceneSelectionComp); }

ecs_view_define(GlobalDrawView) {
  ecs_access_read(DebugInspectorSettingsComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(DebugTextComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugInspectorPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(SubjectView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_write(SceneTransformComp);
  ecs_access_maybe_write(SceneScaleComp);
  ecs_access_maybe_read(SceneNameComp);
  ecs_access_maybe_read(SceneCollisionComp);
  ecs_access_maybe_read(SceneBoundsComp);
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

static void inspector_panel_draw_value_string(UiCanvasComp* canvas, const String value) {
  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);
  ui_label(canvas, value, .selectable = true);
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

static void inspector_panel_draw_entity_info(
    EcsWorld* world, UiCanvasComp* canvas, UiTable* table, EcsIterator* subject) {
  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Entity identifier"));
  ui_table_next_column(canvas, table);
  if (subject) {
    const EcsEntityId entity = ecs_view_entity(subject);
    inspector_panel_draw_value_string(canvas, fmt_write_scratch("{}", fmt_int(entity, .base = 16)));
  } else {
    inspector_panel_draw_value_none(canvas);
  }

  ui_table_next_row(canvas, table);
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

  ui_table_next_row(canvas, table);
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
    UiTable*                 table,
    DebugInspectorPanelComp* panelComp,
    EcsIterator*             subject) {
  if (!subject) {
    return;
  }
  SceneTransformComp* transform = ecs_view_write_t(subject, SceneTransformComp);
  SceneScaleComp*     scale     = ecs_view_write_t(subject, SceneScaleComp);
  if (!transform && !scale) {
    return;
  }
  ui_table_next_row(canvas, table);
  if (!inspector_panel_section(canvas, string_lit("Transform"))) {
    return;
  }
  if (transform) {
    ui_table_next_row(canvas, table);
    ui_label(canvas, string_lit("Position"));
    ui_table_next_column(canvas, table);
    inspector_panel_draw_editor_vec(canvas, &transform->position, 3);

    ui_table_next_row(canvas, table);
    ui_label(canvas, string_lit("Rotation"));
    ui_table_next_column(canvas, table);
    if (inspector_panel_draw_editor_vec(canvas, &panelComp->transformRotEulerDeg, 3)) {
      const GeoVector eulerRad = geo_vector_mul(panelComp->transformRotEulerDeg, math_deg_to_rad);
      transform->rotation      = geo_quat_from_euler(eulerRad);
    } else {
      const GeoVector eulerRad        = geo_quat_to_euler(transform->rotation);
      panelComp->transformRotEulerDeg = geo_vector_mul(eulerRad, math_rad_to_deg);
    }
  }
  if (scale) {
    ui_table_next_row(canvas, table);
    ui_label(canvas, string_lit("Scale"));
    ui_table_next_column(canvas, table);
    inspector_panel_draw_editor_float(canvas, &scale->scale);
  }
}

static void inspector_panel_draw_components(
    EcsWorld* world, UiCanvasComp* canvas, UiTable* table, EcsIterator* subject) {
  if (!subject) {
    return;
  }
  const EcsArchetypeId archetype = ecs_world_entity_archetype(world, ecs_view_entity(subject));
  const BitSet         compMask  = ecs_world_component_mask(world, archetype);
  const u32            compCount = (u32)bitset_count(compMask);

  ui_table_next_row(canvas, table);
  if (inspector_panel_section(canvas, fmt_write_scratch("Components ({})", fmt_int(compCount)))) {
    const EcsDef* def = ecs_world_def(world);
    bitset_for(compMask, compId) {
      const String compName = ecs_def_comp_name(def, (EcsCompId)compId);
      const usize  compSize = ecs_def_comp_size(def, (EcsCompId)compId);
      ui_table_next_row(canvas, table);
      ui_label(canvas, compName);
      ui_table_next_column(canvas, table);
      inspector_panel_draw_value_string(
          canvas, fmt_write_scratch("id: {<3} size: {}", fmt_int(compId), fmt_size(compSize)));
    }
  }
}

static void inspector_panel_draw_settings(
    UiCanvasComp* canvas, UiTable* table, DebugInspectorSettingsComp* settings) {
  ui_table_next_row(canvas, table);
  if (!inspector_panel_section(canvas, string_lit("Settings"))) {
    return;
  }

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Draw pivot"));
  ui_table_next_column(canvas, table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawPivot);

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Draw orientation"));
  ui_table_next_column(canvas, table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawOrientation);

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Draw name"));
  ui_table_next_column(canvas, table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawName);

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Draw collision"));
  ui_table_next_column(canvas, table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawCollision);

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Draw bounds local"));
  ui_table_next_column(canvas, table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawBoundsLocal);

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Draw bounds global"));
  ui_table_next_column(canvas, table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugInspectorFlags_DrawBoundsGlobal);
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
  ui_table_add_column(&table, UiTableColumn_Fixed, 175);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  inspector_panel_draw_entity_info(world, canvas, &table, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_transform(canvas, &table, panelComp, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_components(world, canvas, &table, subject);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  inspector_panel_draw_settings(canvas, &table, settings);
  ui_canvas_id_block_next(canvas); // Draws a variable amount of elements; Skip over the id space.

  ui_panel_end(canvas, &panelComp->panel);
}

static DebugInspectorSettingsComp* inspector_settings_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, SettingsWriteView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  return itr ? ecs_view_write_t(itr, DebugInspectorSettingsComp)
             : ecs_world_add_t(world, ecs_world_global(world), DebugInspectorSettingsComp);
}

ecs_system_define(DebugInspectorUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
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

static void inspector_draw_collision(
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

static void inspector_draw_bounds_local(
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

static void inspector_draw_bounds_global(
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

ecs_system_define(DebugInspectorDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalDrawView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const DebugInspectorSettingsComp* settings =
      ecs_view_read_t(globalItr, DebugInspectorSettingsComp);
  if (!(settings->flags & DebugInspectorFlags_DrawAny)) {
    return;
  }

  DebugShapeComp* shape = ecs_view_write_t(globalItr, DebugShapeComp);
  DebugTextComp*  text  = ecs_view_write_t(globalItr, DebugTextComp);

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, SubjectView)); ecs_view_walk(itr);) {
    const SceneTransformComp* transformComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneNameComp*      nameComp      = ecs_view_read_t(itr, SceneNameComp);
    const SceneBoundsComp*    boundsComp    = ecs_view_read_t(itr, SceneBoundsComp);
    const SceneCollisionComp* collisionComp = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneScaleComp*     scaleComp     = ecs_view_read_t(itr, SceneScaleComp);

    if (settings->flags & DebugInspectorFlags_DrawPivot) {
      const GeoColor color = geo_color(1.0f, 1.0f, 0.0f, 1.0f);
      debug_sphere(shape, transformComp->position, 0.025f, color, DebugShape_Overlay);
    }
    if (settings->flags & DebugInspectorFlags_DrawOrientation) {
      debug_orientation(shape, transformComp->position, transformComp->rotation, 0.25f);
    }
    if (nameComp && settings->flags & DebugInspectorFlags_DrawName) {
      const String    name = stringtable_lookup(g_stringtable, nameComp->name);
      const GeoVector pos  = geo_vector_add(transformComp->position, geo_vector_mul(geo_up, 0.1f));
      debug_text(text, pos, name, geo_color_white);
    }
    if (collisionComp && settings->flags & DebugInspectorFlags_DrawCollision) {
      inspector_draw_collision(shape, collisionComp, transformComp, scaleComp);
    }
    if (boundsComp && !geo_box_is_inverted3(&boundsComp->local)) {
      if (settings->flags & DebugInspectorFlags_DrawBoundsLocal) {
        inspector_draw_bounds_local(shape, boundsComp, transformComp, scaleComp);
      }
      if (settings->flags & DebugInspectorFlags_DrawBoundsGlobal) {
        inspector_draw_bounds_global(shape, boundsComp, transformComp, scaleComp);
      }
    }
  }
}

ecs_module_init(debug_inspector_module) {
  ecs_register_comp(DebugInspectorSettingsComp);
  ecs_register_comp(DebugInspectorPanelComp);

  ecs_register_view(SettingsWriteView);
  ecs_register_view(GlobalUpdateView);
  ecs_register_view(GlobalDrawView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(SubjectView);

  ecs_register_system(
      DebugInspectorUpdatePanelSys,
      ecs_view_id(GlobalUpdateView),
      ecs_view_id(SettingsWriteView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(SubjectView));

  ecs_register_system(DebugInspectorDrawSys, ecs_view_id(GlobalDrawView), ecs_view_id(SubjectView));

  ecs_order(DebugInspectorDrawSys, DebugOrder_InspectorDebugDraw);
}

EcsEntityId debug_inspector_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugInspectorPanelComp, .panel = ui_panel(ui_vector(450, 350)));
  return panelEntity;
}
