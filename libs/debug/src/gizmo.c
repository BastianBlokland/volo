#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "debug_gizmo.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "geo_query.h"
#include "input.h"
#include "scene_camera.h"
#include "scene_transform.h"

typedef enum {
  DebugGizmoAxis_X,
  DebugGizmoAxis_Y,
  DebugGizmoAxis_Z,

  DebugGizmoAxis_Count,
} DebugGizmoAxis;

static const struct {
  GeoVector dir;
  f32       length, radius;
  GeoColor  colorNormal, colorHovered;
} g_gizmoTranslationArrows[] = {
    [DebugGizmoAxis_X] =
        {
            .dir          = {1, 0, 0},
            .length       = 0.5f,
            .radius       = 0.05f,
            .colorNormal  = {0.4f, 0, 0, 0.75f},
            .colorHovered = {1, 0.05f, 0.05f, 1},
        },
    [DebugGizmoAxis_Y] =
        {
            .dir          = {0, 1, 0},
            .length       = 0.5f,
            .radius       = 0.05f,
            .colorNormal  = {0, 0.4f, 0, 0.75f},
            .colorHovered = {0.05f, 1, 0.05f, 1},
        },
    [DebugGizmoAxis_Z] =
        {
            .dir          = {0, 0, 1},
            .length       = 0.5f,
            .radius       = 0.05f,
            .colorNormal  = {0, 0, 0.4f, 0.75f},
            .colorHovered = {0.05f, 0.05f, 1, 1},
        },
};

typedef enum {
  DebugGizmoType_Translation,

  DebugGizmoType_Count,
} DebugGizmoType;

typedef struct {
  DebugGizmoType type;
  DebugGizmoId   id;
  union {
    struct {
      GeoVector pos;
      GeoQuat   rot;
    } data_translation;
  };
} DebugGizmo;

typedef enum {
  DebugGizmoStatus_None,
  DebugGizmoStatus_Hovering,
  DebugGizmoStatus_Interacting,
} DebugGizmoStatus;

typedef struct {
  DebugGizmoType   type;
  DebugGizmoStatus status;
  DebugGizmoId     activeId;
  DebugGizmoAxis   activeAxis;
  union {
    struct {
      GeoVector startPos;
      GeoQuat   startRot;
    } data_translation;
  };
} DebugGizmoEditor;

ecs_comp_define(DebugGizmoComp) {
  DebugGizmoEditor editor;
  DynArray         entries; // DebugGizmo[]
  GeoQueryEnv*     queryEnv;
};

static void ecs_destruct_gizmo(void* data) {
  DebugGizmoComp* comp = data;
  dynarray_destroy(&comp->entries);
  geo_query_env_destroy(comp->queryEnv);
}

static bool debug_gizmo_is_hovered(const DebugGizmoComp* comp, const DebugGizmoId id) {
  return comp->editor.status >= DebugGizmoStatus_Hovering && comp->editor.activeId == id;
}

static bool debug_gizmo_is_hovered_axis(
    const DebugGizmoComp* comp, const DebugGizmoId id, const DebugGizmoAxis axis) {
  return debug_gizmo_is_hovered(comp, id) && comp->editor.activeAxis == axis;
}

ecs_view_define(GlobalUpdateView) {
  ecs_access_write(DebugGizmoComp);
  ecs_access_write(InputManagerComp);
}

ecs_view_define(GlobalRenderView) {
  ecs_access_read(DebugGizmoComp);
  ecs_access_write(DebugShapeComp);
}

ecs_view_define(CameraView) {
  ecs_access_read(GapWindowComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

static u64 debug_gizmo_shape_id(const u32 i, const DebugGizmoAxis a) { return i | ((u64)a << 32u); }
static u32 debug_gizmo_shape_index(const u64 id) { return (u32)id; }
static DebugGizmoAxis debug_gizmo_shape_axis(const u64 id) { return (DebugGizmoAxis)(id >> 32u); }

static void
debug_gizmo_register_translation(DebugGizmoComp* comp, const u32 index, const DebugGizmo* gizmo) {
  diag_assert(gizmo->type == DebugGizmoType_Translation);

  // Register collision shapes for the translation arrows.
  const GeoQuat rot = gizmo->data_translation.rot;
  for (DebugGizmoAxis a = 0; a != DebugGizmoAxis_Count; ++a) {
    const GeoVector dir       = geo_quat_rotate(rot, g_gizmoTranslationArrows[a].dir);
    const f32       length    = g_gizmoTranslationArrows[a].length;
    const GeoVector lineStart = gizmo->data_translation.pos;
    const GeoVector lineEnd   = geo_vector_add(lineStart, geo_vector_mul(dir, length));

    geo_query_insert_capsule(
        comp->queryEnv,
        (GeoCapsule){
            .line   = {.a = lineStart, .b = lineEnd},
            .radius = g_gizmoTranslationArrows[a].radius,
        },
        debug_gizmo_shape_id(index, a));
  }
}

static void debug_gizmo_register(DebugGizmoComp* comp, const u32 index, const DebugGizmo* gizmo) {
  switch (gizmo->type) {
  case DebugGizmoType_Translation:
    return debug_gizmo_register_translation(comp, index, gizmo);
  case DebugGizmoType_Count:
    break;
  }
  diag_crash();
}

static void debug_gizmo_update_interaction(
    DebugGizmoComp* comp, DebugGizmoEditor* editor, const GeoRay* inputRay) {
  (void)comp;
  (void)editor;
  (void)inputRay;
}

static void debug_gizmo_update_editor(
    DebugGizmoComp*           comp,
    DebugGizmoEditor*         editor,
    const InputManagerComp*   input,
    const GapWindowComp*      window,
    const SceneCameraComp*    camera,
    const SceneTransformComp* cameraTrans) {

  const bool      inputDown    = gap_window_key_down(window, GapKey_MouseLeft);
  const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
  const f32       inputAspect  = input_cursor_aspect(input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);
  const bool      hoveringUi   = (input_blockers(input) & InputBlocker_HoveringUi) != 0;

  const DebugGizmo* hoveredGizmo = null;
  DebugGizmoAxis    hoveredAxis  = 0;
  GeoQueryRayHit    hit;
  if (!hoveringUi && geo_query_ray(comp->queryEnv, &inputRay, &hit)) {
    const u32 hoveredIndex = debug_gizmo_shape_index(hit.shapeId);
    hoveredGizmo           = dynarray_at_t(&comp->entries, hoveredIndex, DebugGizmo);
    hoveredAxis            = debug_gizmo_shape_axis(hit.shapeId);
  }

  if (editor->status == DebugGizmoStatus_None && hoveredGizmo) {
    editor->status     = DebugGizmoStatus_Hovering;
    editor->activeId   = hoveredGizmo->id;
    editor->activeAxis = hoveredAxis;
    return;
  }

  const bool isHovering    = editor->status == DebugGizmoStatus_Hovering;
  const bool isInteracting = editor->status == DebugGizmoStatus_Interacting;

  if ((isHovering && !hoveredGizmo) || (isInteracting && !inputDown)) {
    editor->status = DebugGizmoStatus_None;
    return;
  }

  if (isHovering && (editor->activeId != hoveredGizmo->id || editor->activeAxis != hoveredAxis)) {
    editor->activeId   = hoveredGizmo->id;
    editor->activeAxis = hoveredAxis;
    return;
  }

  if (isHovering && inputDown) {
    editor->status = DebugGizmoStatus_Interacting;
    return;
  }

  if (isInteracting) {
    debug_gizmo_update_interaction(comp, editor, &inputRay);
  }
}

ecs_system_define(DebugGizmoUpdateSys) {
  // Initialize the global gizmo component.
  if (!ecs_world_has_t(world, ecs_world_global(world), DebugGizmoComp)) {
    ecs_world_add_t(
        world,
        ecs_world_global(world),
        DebugGizmoComp,
        .entries  = dynarray_create_t(g_alloc_heap, DebugGizmo, 8),
        .queryEnv = geo_query_env_create(g_alloc_heap));
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DebugGizmoComp*   gizmoComp = ecs_view_write_t(globalItr, DebugGizmoComp);
  InputManagerComp* input     = ecs_view_write_t(globalItr, InputManagerComp);

  // Register all gizmos that where active in the last frame.
  geo_query_env_clear(gizmoComp->queryEnv);
  for (u32 i = 0; i != gizmoComp->entries.size; ++i) {
    debug_gizmo_register(gizmoComp, i, dynarray_at_t(&gizmoComp->entries, i, DebugGizmo));
  }

  // Update the editor.
  EcsView* cameraView = ecs_world_view_t(world, CameraView);
  if (ecs_view_contains(cameraView, input_active_window(input))) {
    EcsIterator*              camItr      = ecs_view_at(cameraView, input_active_window(input));
    const GapWindowComp*      window      = ecs_view_read_t(camItr, GapWindowComp);
    const SceneCameraComp*    camera      = ecs_view_read_t(camItr, SceneCameraComp);
    const SceneTransformComp* cameraTrans = ecs_view_read_t(camItr, SceneTransformComp);

    debug_gizmo_update_editor(gizmoComp, &gizmoComp->editor, input, window, camera, cameraTrans);
  }

  // Update input blockers.
  input_blocker_update(input, InputBlocker_HoveringGizmo, gizmoComp->editor.status > 0);

  // Clear last frame's entries.
  dynarray_clear(&gizmoComp->entries);
}

void debug_gizmo_draw_translation(
    const DebugGizmoComp* comp, DebugShapeComp* shape, const DebugGizmo* gizmo) {
  diag_assert(gizmo->type == DebugGizmoType_Translation);

  // Draw all translation arrows.
  const GeoVector pos = gizmo->data_translation.pos;
  const GeoQuat   rot = gizmo->data_translation.rot;
  for (DebugGizmoAxis a = 0; a != DebugGizmoAxis_Count; ++a) {
    const bool      isHovered = debug_gizmo_is_hovered_axis(comp, gizmo->id, a);
    const GeoVector dir       = geo_quat_rotate(rot, g_gizmoTranslationArrows[a].dir);
    const f32       length    = g_gizmoTranslationArrows[a].length;
    const f32       radius    = g_gizmoTranslationArrows[a].radius;
    const GeoVector lineStart = geo_vector_add(pos, geo_vector_mul(dir, 0.02f));
    const GeoVector lineEnd   = geo_vector_add(pos, geo_vector_mul(dir, length));
    const GeoColor  color     = isHovered ? g_gizmoTranslationArrows[a].colorHovered
                                          : g_gizmoTranslationArrows[a].colorNormal;

    debug_arrow(shape, lineStart, lineEnd, radius, color);
  }
}

ecs_system_define(DebugGizmoRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalRenderView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const DebugGizmoComp* gizmoComp = ecs_view_read_t(globalItr, DebugGizmoComp);
  DebugShapeComp*       shape     = ecs_view_write_t(globalItr, DebugShapeComp);

  dynarray_for_t(&gizmoComp->entries, DebugGizmo, entry) {
    switch (entry->type) {
    case DebugGizmoType_Translation:
      debug_gizmo_draw_translation(gizmoComp, shape, entry);
      break;
    case DebugGizmoType_Count:
      break;
    }
  }
}

ecs_module_init(debug_gizmo_module) {
  ecs_register_comp(DebugGizmoComp, .destructor = ecs_destruct_gizmo);

  ecs_register_view(GlobalUpdateView);
  ecs_register_view(GlobalRenderView);
  ecs_register_view(CameraView);

  ecs_register_system(DebugGizmoUpdateSys, ecs_view_id(GlobalUpdateView), ecs_view_id(CameraView));
  ecs_order(DebugGizmoUpdateSys, DebugOrder_GizmoUpdate);

  ecs_register_system(DebugGizmoRenderSys, ecs_view_id(GlobalRenderView));
  ecs_order(DebugGizmoRenderSys, DebugOrder_GizmoRender);
}

bool debug_gizmo_translation(
    DebugGizmoComp* comp, const DebugGizmoId id, GeoVector* translation, const GeoQuat rotation) {

  *dynarray_push_t(&comp->entries, DebugGizmo) = (DebugGizmo){
      .type             = DebugGizmoType_Translation,
      .id               = id,
      .data_translation = {.pos = *translation, .rot = rotation},
  };
  return false;
}
