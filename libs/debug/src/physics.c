#include "debug_physics.h"
#include "debug_shape.h"
#include "ecs_world.h"
#include "scene_bounds.h"
#include "scene_transform.h"
#include "ui.h"

ecs_comp_define(DebugPhysicsPanelComp) {
  UiPanel     panel;
  EcsEntityId window;
};

ecs_view_define(GlobalView) { ecs_access_write(DebugShapeCanvasComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugPhysicsPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(ObjectView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneBoundsComp);
  ecs_access_maybe_read(SceneScaleComp);
}

static void physics_panel_draw(UiCanvasComp* canvas, DebugPhysicsPanelComp* panelComp) {
  const String title = fmt_write_scratch("{} Physics Debug", fmt_ui_shape(ViewInAr));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);
  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugPhysicsUpdatePanelSys) {
  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId      entity    = ecs_view_entity(itr);
    DebugPhysicsPanelComp* panelComp = ecs_view_write_t(itr, DebugPhysicsPanelComp);
    UiCanvasComp*          canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    physics_panel_draw(canvas, panelComp);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

static void physics_draw_bounds_rotated(
    DebugShapeCanvasComp* shapeCanvas,
    const GeoVector       position,
    const GeoQuat         rotation,
    const GeoBox          bounds,
    const f32             scale) {
  const GeoBox aabb = geo_box_transform3(&bounds, position, geo_quat_ident, scale);
  debug_shape_box_fill(shapeCanvas, aabb, rotation, geo_color(0.0f, 1.0f, 0.0f, 0.2f));
  debug_shape_box_wire(shapeCanvas, aabb, rotation, geo_color(0.0f, 1.0f, 0.0f, 0.5f));
}

static void physics_draw_bounds_aligned(
    DebugShapeCanvasComp* shapeCanvas,
    const GeoVector       position,
    const GeoQuat         rotation,
    const GeoBox          bounds,
    const f32             scale) {
  const GeoBox aabb = geo_box_transform3(&bounds, position, rotation, scale);
  debug_shape_box_fill(shapeCanvas, aabb, geo_quat_ident, geo_color(0.0f, 0.0f, 1.0f, 0.2f));
  debug_shape_box_wire(shapeCanvas, aabb, geo_quat_ident, geo_color(0.0f, 0.0f, 1.0f, 0.5f));
}

ecs_system_define(DebugPhysicsDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DebugShapeCanvasComp* shapeCanvas = ecs_view_write_t(globalItr, DebugShapeCanvasComp);

  EcsView* objectView = ecs_world_view_t(world, ObjectView);
  for (EcsIterator* itr = ecs_view_itr(objectView); ecs_view_walk(itr);) {
    const GeoVector       position  = ecs_view_read_t(itr, SceneTransformComp)->position;
    const GeoQuat         rotation  = ecs_view_read_t(itr, SceneTransformComp)->rotation;
    const GeoBox          bounds    = ecs_view_read_t(itr, SceneBoundsComp)->local;
    const SceneScaleComp* scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const f32             scale     = scaleComp ? scaleComp->scale : 1.0f;

    physics_draw_bounds_rotated(shapeCanvas, position, rotation, bounds, scale);
    physics_draw_bounds_aligned(shapeCanvas, position, rotation, bounds, scale);
  }
}

ecs_module_init(debug_physics_module) {
  ecs_register_comp(DebugPhysicsPanelComp);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(ObjectView);

  ecs_register_system(DebugPhysicsUpdatePanelSys, ecs_view_id(PanelUpdateView));
  ecs_register_system(DebugPhysicsDrawSys, ecs_view_id(GlobalView), ecs_view_id(ObjectView));
}

EcsEntityId debug_physics_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugPhysicsPanelComp,
      .panel  = ui_panel(ui_vector(330, 255)),
      .window = window);
  return panelEntity;
}
