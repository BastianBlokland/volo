#include "core_array.h"
#include "core_math.h"
#include "debug_camera.h"
#include "ecs_world.h"
#include "scene_camera.h"
#include "scene_tag.h"
#include "scene_transform.h"
#include "ui.h"

// clang-format off

static const String g_tooltipOrthoSize      = string_static("Size (in meters) of the dominant dimension of the orthographic projection.");
static const String g_tooltipFov            = string_static("Field of view of the dominant dimension of the perspective projection.");
static const String g_tooltipVerticalAspect = string_static("Use the vertical dimension as the dominant dimension.");
static const String g_tooltipNearDistance   = string_static("Distance (in meters) to the near clipping plane.");
static const String g_tooltipExclude        = string_static("Exclude {} from being rendered.");
static const String g_tooltipMoveSpeed      = string_static("Camera movement speed in meters per second.");
static const String g_tooltipDefaults       = string_static("Reset all settings to their defaults.");

// clang-format on

static const String g_projectionNames[] = {
    string_static("Perspective"),
    string_static("Orthographic"),
};

ecs_comp_define(DebugCameraPanelComp) {
  UiPanelState state;
  EcsEntityId  window;
};

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugCameraPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(WindowView) {
  ecs_access_write(SceneCameraComp);
  ecs_access_maybe_write(SceneCameraMovementComp);
  ecs_access_maybe_write(SceneTransformComp);
}

static void camera_default_transform(const SceneCameraComp* camera, SceneTransformComp* transform) {
  if (camera->flags & SceneCameraFlags_Orthographic) {
    transform->position = geo_vector(0);
    transform->rotation = geo_quat_look(geo_down, geo_forward);
  } else {
    transform->position = geo_vector(0, 1.5f, -3.0f);
    transform->rotation = geo_quat_angle_axis(geo_right, 10 * math_deg_to_rad);
  }
}

static void camera_panel_draw_ortho(
    UiCanvasComp* canvas, UiTable* table, SceneCameraComp* camera, SceneTransformComp* transform) {

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Size"));
  ui_table_next_column(canvas, table);
  ui_slider(canvas, &camera->orthoSize, .min = 1, .max = 100, .tooltip = g_tooltipOrthoSize);

  if (transform) {
    ui_table_next_row(canvas, table);
    if (ui_button(canvas, .label = string_lit("Top"))) {
      transform->position = geo_vector(0);
      transform->rotation = geo_quat_look(geo_down, geo_forward);
    }
    ui_table_next_column(canvas, table);
    if (ui_button(canvas, .label = string_lit("Front"))) {
      transform->position = geo_vector(0);
      transform->rotation = geo_quat_look(geo_forward, geo_up);
    }
  }
}

static void camera_panel_draw_pers(UiCanvasComp* canvas, UiTable* table, SceneCameraComp* camera) {

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Field of view"));
  ui_table_next_column(canvas, table);
  f32 fovDegrees = camera->persFov * math_rad_to_deg;
  if (ui_slider(canvas, &fovDegrees, .min = 10, .max = 150, .tooltip = g_tooltipFov)) {
    camera->persFov = fovDegrees * math_deg_to_rad;
  }

  ui_table_next_row(canvas, table);
  ui_label(canvas, string_lit("Near distance"));
  ui_table_next_column(canvas, table);
  ui_slider(canvas, &camera->persNear, .min = 0.001f, .max = 5, .tooltip = g_tooltipNearDistance);
}

static void
camera_panel_draw_filters(UiCanvasComp* canvas, UiTable* table, SceneCameraComp* camera) {
  static const struct {
    SceneTags tag;
    String    name;
  } g_filters[] = {
      {SceneTags_Geometry, string_static("geometry")},
      {SceneTags_Debug, string_static("debug")},
  };

  for (usize i = 0; i != array_elems(g_filters); ++i) {
    const String name = g_filters[i].name;
    const String tooltip =
        format_write_formatted_scratch(g_tooltipExclude, fmt_args(fmt_text(name)));

    ui_table_next_row(canvas, table);
    ui_label(canvas, fmt_write_scratch("Exclude {}", fmt_text(name)));
    ui_table_next_column(canvas, table);
    bool illegal = (camera->filter.illegal & g_filters[i].tag) != 0;
    if (ui_toggle(canvas, &illegal, .tooltip = tooltip)) {
      camera->filter.illegal ^= g_filters[i].tag;
    }
  }
}

static void camera_panel_draw(
    UiCanvasComp*            canvas,
    DebugCameraPanelComp*    panel,
    SceneCameraComp*         camera,
    SceneCameraMovementComp* cameraMovement,
    SceneTransformComp*      transform) {
  const String title = fmt_write_scratch("{} Camera Settings", fmt_ui_shape(PhotoCamera));
  ui_panel_begin(canvas, &panel->state, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Projection"));
  ui_table_next_column(canvas, &table);
  i32 projectionIdx = (camera->flags & SceneCameraFlags_Orthographic) != 0;
  if (ui_select(canvas, &projectionIdx, g_projectionNames, 2)) {
    if (projectionIdx == 1) {
      camera->flags |= SceneCameraFlags_Orthographic;
    } else {
      camera->flags &= ~SceneCameraFlags_Orthographic;
    }
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Vertical aspect"));
  ui_table_next_column(canvas, &table);
  bool vertical = (camera->flags & SceneCameraFlags_Vertical) != 0;
  if (ui_toggle(canvas, &vertical, .tooltip = g_tooltipVerticalAspect)) {
    camera->flags ^= SceneCameraFlags_Vertical;
  }

  if (projectionIdx == 1) {
    camera_panel_draw_ortho(canvas, &table, camera, transform);
  } else {
    camera_panel_draw_pers(canvas, &table, camera);
  }

  camera_panel_draw_filters(canvas, &table, camera);

  if (cameraMovement) {
    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("Move speed"));
    ui_table_next_column(canvas, &table);
    f64 moveSpeed = cameraMovement->moveSpeed;
    if (ui_numbox(canvas, &moveSpeed, .tooltip = g_tooltipMoveSpeed)) {
      cameraMovement->moveSpeed = (f32)moveSpeed;
    }
  }

  ui_table_next_row(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Defaults"), .tooltip = g_tooltipDefaults)) {
    scene_camera_to_default(camera);
    if (cameraMovement) {
      scene_camera_movement_to_default(cameraMovement);
    }
    if (transform) {
      camera_default_transform(camera, transform);
    }
  }

  ui_panel_end(canvas, &panel->state);
}

ecs_system_define(DebugCameraUpdatePanelSys) {
  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, WindowView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugCameraPanelComp* panel  = ecs_view_write_t(itr, DebugCameraPanelComp);
    UiCanvasComp*         canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, panel->window)) {
      continue; // Window has been destroyed, or has no camera.
    }
    SceneCameraComp*         camera         = ecs_view_write_t(windowItr, SceneCameraComp);
    SceneCameraMovementComp* cameraMovement = ecs_view_write_t(windowItr, SceneCameraMovementComp);

    SceneTransformComp* transform = ecs_view_write_t(windowItr, SceneTransformComp);

    ui_canvas_reset(canvas);
    camera_panel_draw(canvas, panel, camera, cameraMovement, transform);

    if (panel->state.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_camera_module) {
  ecs_register_comp(DebugCameraPanelComp);

  ecs_register_view(PanelUpdateView);
  ecs_register_view(WindowView);

  ecs_register_system(
      DebugCameraUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(WindowView));
}

EcsEntityId debug_camera_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugCameraPanelComp,
      .state  = ui_panel_init(ui_vector(330, 290)),
      .window = window);
  return panelEntity;
}
