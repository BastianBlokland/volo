#include "asset_manager.h"
#include "debug_physics.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_bounds.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_renderable.h"
#include "scene_transform.h"
#include "ui.h"

typedef enum {
  DebugPhysicsFlags_DrawPivot        = 1 << 0,
  DebugPhysicsFlags_DrawOrientation  = 1 << 1,
  DebugPhysicsFlags_DrawCollision    = 1 << 2,
  DebugPhysicsFlags_DrawBoundsLocal  = 1 << 3,
  DebugPhysicsFlags_DrawBoundsGlobal = 1 << 4,

  DebugPhysicsFlags_DrawAny = DebugPhysicsFlags_DrawPivot | DebugPhysicsFlags_DrawOrientation |
                              DebugPhysicsFlags_DrawCollision | DebugPhysicsFlags_DrawBoundsLocal |
                              DebugPhysicsFlags_DrawBoundsGlobal
} DebugPhysicsFlags;

ecs_comp_define(DebugPhysicsSettingsComp) { DebugPhysicsFlags flags; };

ecs_comp_define(DebugPhysicsPanelComp) { UiPanel panel; };

ecs_view_define(SettingsUpdateView) { ecs_access_write(DebugPhysicsSettingsComp); }

ecs_view_define(GlobalDrawView) {
  ecs_access_read(DebugPhysicsSettingsComp);
  ecs_access_write(DebugShapeComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugPhysicsPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(ObjectView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_maybe_read(SceneCollisionComp);
  ecs_access_maybe_read(SceneBoundsComp);
  ecs_access_maybe_read(SceneScaleComp);
}

static void physics_panel_draw(
    UiCanvasComp* canvas, DebugPhysicsPanelComp* panelComp, DebugPhysicsSettingsComp* settings) {
  const String title = fmt_write_scratch("{} Physics Debug", fmt_ui_shape(ViewInAr));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 175);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw pivot"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugPhysicsFlags_DrawPivot);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw orientation"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugPhysicsFlags_DrawOrientation);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw collision"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugPhysicsFlags_DrawCollision);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw bounds local"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugPhysicsFlags_DrawBoundsLocal);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Draw bounds global"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(canvas, (u32*)&settings->flags, DebugPhysicsFlags_DrawBoundsGlobal);

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugPhysicsUpdatePanelSys) {
  EcsView*                  settingsView = ecs_world_view_t(world, SettingsUpdateView);
  EcsIterator*              settingsItr  = ecs_view_maybe_at(settingsView, ecs_world_global(world));
  DebugPhysicsSettingsComp* settings =
      settingsItr ? ecs_view_write_t(settingsItr, DebugPhysicsSettingsComp)
                  : ecs_world_add_t(world, ecs_world_global(world), DebugPhysicsSettingsComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId      entity    = ecs_view_entity(itr);
    DebugPhysicsPanelComp* panelComp = ecs_view_write_t(itr, DebugPhysicsPanelComp);
    UiCanvasComp*          canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    physics_panel_draw(canvas, panelComp, settings);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

static void physics_draw_collision(
    DebugShapeComp*           shape,
    const SceneCollisionComp* collision,
    const SceneTransformComp* transform,
    const SceneScaleComp*     scale) {
  switch (collision->type) {
  case SceneCollisionType_Capsule: {
    const GeoCapsule c = scene_collision_world_capsule(&collision->capsule, transform, scale);
    const GeoColor   colorFill = geo_color(1, 0, 0, 0.2f);
    const GeoColor   colorWire = geo_color(1, 0, 0, 1);
    debug_capsule(shape, c.line.a, c.line.b, c.radius, colorFill, DebugShape_Fill);
    debug_capsule(shape, c.line.a, c.line.b, c.radius, colorWire, DebugShape_Wire);
  } break;
  }
}

static void physics_draw_bounds_local(
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

static void physics_draw_bounds_global(
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

ecs_system_define(DebugPhysicsDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalDrawView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const DebugPhysicsSettingsComp* settings = ecs_view_read_t(globalItr, DebugPhysicsSettingsComp);
  if (!(settings->flags & DebugPhysicsFlags_DrawAny)) {
    return;
  }

  DebugShapeComp* shape = ecs_view_write_t(globalItr, DebugShapeComp);

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ObjectView)); ecs_view_walk(itr);) {
    const SceneTransformComp* transformComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneBoundsComp*    boundsComp    = ecs_view_read_t(itr, SceneBoundsComp);
    const SceneCollisionComp* collisionComp = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneScaleComp*     scaleComp     = ecs_view_read_t(itr, SceneScaleComp);

    if (settings->flags & DebugPhysicsFlags_DrawPivot) {
      const GeoColor color = geo_color(1.0f, 1.0f, 0.0f, 1.0f);
      debug_sphere(shape, transformComp->position, 0.025f, color, DebugShape_Overlay);
    }
    if (settings->flags & DebugPhysicsFlags_DrawOrientation) {
      debug_orientation(shape, transformComp->position, transformComp->rotation, 0.25f);
    }
    if (collisionComp && settings->flags & DebugPhysicsFlags_DrawCollision) {
      physics_draw_collision(shape, collisionComp, transformComp, scaleComp);
    }
    if (boundsComp && !geo_box_is_inverted3(&boundsComp->local)) {
      if (settings->flags & DebugPhysicsFlags_DrawBoundsLocal) {
        physics_draw_bounds_local(shape, boundsComp, transformComp, scaleComp);
      }
      if (settings->flags & DebugPhysicsFlags_DrawBoundsGlobal) {
        physics_draw_bounds_global(shape, boundsComp, transformComp, scaleComp);
      }
    }
  }
}

ecs_module_init(debug_physics_module) {
  ecs_register_comp(DebugPhysicsSettingsComp);
  ecs_register_comp(DebugPhysicsPanelComp);

  ecs_register_view(SettingsUpdateView);
  ecs_register_view(GlobalDrawView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(ObjectView);

  ecs_register_system(
      DebugPhysicsUpdatePanelSys, ecs_view_id(SettingsUpdateView), ecs_view_id(PanelUpdateView));

  ecs_register_system(DebugPhysicsDrawSys, ecs_view_id(GlobalDrawView), ecs_view_id(ObjectView));

  ecs_order(DebugPhysicsDrawSys, DebugOrder_PhysicsDebugDraw);
}

EcsEntityId debug_physics_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugPhysicsPanelComp, .panel = ui_panel(ui_vector(330, 255)));
  return panelEntity;
}
