#include "core_format.h"
#include "core_math.h"
#include "debug_gizmo.h"
#include "debug_shape.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "input_manager.h"
#include "rend_light.h"
#include "ui.h"

ecs_comp_define(DebugLightPanelComp) {
  UiPanel   panel;
  GeoVector sunRotEulerDeg; // Local copy of rotation as euler angles to use while editing.
};

ecs_view_define(GlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_write(DebugGizmoComp);
  ecs_access_write(DebugShapeComp);
  ecs_access_write(RendLightSettingsComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugLightPanelComp);
  ecs_access_write(UiCanvasComp);
}

static GeoColor radiance_resolve(const GeoColor light) {
  return (GeoColor){
      .r = light.r * light.a,
      .g = light.g * light.a,
      .b = light.b * light.a,
      .a = 1.0f,
  };
}

static bool light_panel_draw_editor_f32(UiCanvasComp* canvas, f32* val) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .min = f32_min, .max = f32_max, .flags = UiWidget_DirtyWhileEditing)) {
    *val = (f32)v;
    return true;
  }
  return false;
}

static bool light_panel_draw_editor_vec(UiCanvasComp* canvas, GeoVector* val, const u8 numComps) {
  static const f32 g_spacing   = 10.0f;
  const u8         numSpacings = numComps - 1;
  const UiAlign    align       = UiAlign_MiddleLeft;
  ui_layout_push(canvas);
  ui_layout_resize(canvas, align, ui_vector(1.0f / numComps, 0), UiBase_Current, Ui_X);
  ui_layout_grow(
      canvas, align, ui_vector(numSpacings * -g_spacing / numComps, 0), UiBase_Absolute, Ui_X);

  bool isDirty = false;
  for (u8 comp = 0; comp != numComps; ++comp) {
    isDirty |= light_panel_draw_editor_f32(canvas, &val->comps[comp]);
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
  ui_layout_pop(canvas);
  return isDirty;
}

static void light_panel_draw_sun(
    UiCanvasComp*          canvas,
    UiTable*               table,
    DebugLightPanelComp*   panelComp,
    RendLightSettingsComp* lightSettings) {

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Sun light"));
  ui_table_next_column(canvas, table);
  light_panel_draw_editor_vec(canvas, (GeoVector*)&lightSettings->sunRadiance, 4);

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Sun rotation"));
  ui_table_next_column(canvas, table);
  if (light_panel_draw_editor_vec(canvas, &panelComp->sunRotEulerDeg, 3)) {
    const GeoVector eulerRad   = geo_vector_mul(panelComp->sunRotEulerDeg, math_deg_to_rad);
    lightSettings->sunRotation = geo_quat_from_euler(eulerRad);
  } else {
    const GeoVector eulerRad  = geo_quat_to_euler(lightSettings->sunRotation);
    panelComp->sunRotEulerDeg = geo_vector_mul(eulerRad, math_rad_to_deg);
  }
}

static void light_panel_draw(
    UiCanvasComp* canvas, DebugLightPanelComp* panelComp, RendLightSettingsComp* lightSettings) {
  const String title = fmt_write_scratch("{} Light Panel", fmt_ui_shape(Light));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  light_panel_draw_sun(canvas, &table, panelComp, lightSettings);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Ambient"));
  ui_table_next_column(canvas, &table);
  ui_slider(canvas, &lightSettings->ambient);

  ui_table_next_row(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Defaults"))) {
    rend_light_settings_to_default(lightSettings);
  }

  ui_panel_end(canvas, &panelComp->panel);
}

static void light_sun_gizmo_draw(
    DebugGizmoComp* gizmo, DebugShapeComp* shape, RendLightSettingsComp* lightSettings) {

  const GeoVector pos = {.y = 10.0f};
  const GeoVector dir = geo_quat_rotate(lightSettings->sunRotation, geo_forward);

  const DebugGizmoId gizmoId = string_hash_lit("SunRotation");
  debug_gizmo_rotation(gizmo, gizmoId, pos, &lightSettings->sunRotation);

  debug_arrow(
      shape,
      pos,
      geo_vector_add(pos, geo_vector_mul(dir, 2.0f)),
      0.25f,
      radiance_resolve(lightSettings->sunRadiance));
}

ecs_system_define(DebugLightUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DebugGizmoComp*        gizmo         = ecs_view_write_t(globalItr, DebugGizmoComp);
  DebugShapeComp*        shape         = ecs_view_write_t(globalItr, DebugShapeComp);
  RendLightSettingsComp* lightSettings = ecs_view_write_t(globalItr, RendLightSettingsComp);

  bool     anyPanelOpen = false;
  EcsView* panelView    = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugLightPanelComp* panelComp = ecs_view_write_t(itr, DebugLightPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    light_panel_draw(canvas, panelComp, lightSettings);
    anyPanelOpen = true;

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }

  if (anyPanelOpen) {
    light_sun_gizmo_draw(gizmo, shape, lightSettings);
  }
}

ecs_module_init(debug_light_module) {
  ecs_register_comp(DebugLightPanelComp);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(DebugLightUpdateSys, ecs_view_id(PanelUpdateView), ecs_view_id(GlobalView));
}

EcsEntityId debug_light_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugLightPanelComp,
      .panel = ui_panel(.position = ui_vector(0.75f, 0.5f), .size = ui_vector(375, 250)));
  return panelEntity;
}
