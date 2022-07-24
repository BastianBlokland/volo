#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "debug_gizmo.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "ecs_world.h"
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
  GeoVector pos;
  GeoQuat   rot;
} DebugGizmoTranslation;

typedef struct {
  DebugGizmoType type;
  DebugGizmoId   id;
  union {
    DebugGizmoTranslation data_translation;
  };
} DebugGizmo;

ecs_comp_define(DebugGizmoComp) {
  bool         isHovering;
  DebugGizmoId activeId;
  DynArray     entries; // DebugGizmo[]
  GeoQueryEnv* queryEnv;
};

static void ecs_destruct_gizmo(void* data) {
  DebugGizmoComp* comp = data;
  dynarray_destroy(&comp->entries);
  geo_query_env_destroy(comp->queryEnv);
}

ecs_view_define(GlobalUpdateView) {
  ecs_access_read(InputManagerComp);
  ecs_access_write(DebugGizmoComp);
}

ecs_view_define(GlobalRenderView) {
  ecs_access_read(DebugGizmoComp);
  ecs_access_write(DebugShapeComp);
}

ecs_view_define(CameraView) {
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

static void debug_gizmo_register_translation(DebugGizmoComp* comp, const DebugGizmo* gizmo) {
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
        gizmo->id);
  }
}

static void debug_gizmo_register(DebugGizmoComp* comp, const DebugGizmo* gizmo) {
  switch (gizmo->type) {
  case DebugGizmoType_Translation:
    return debug_gizmo_register_translation(comp, gizmo);
  case DebugGizmoType_Count:
    break;
  }
  diag_crash();
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
  DebugGizmoComp*         gizmoComp = ecs_view_write_t(globalItr, DebugGizmoComp);
  const InputManagerComp* input     = ecs_view_read_t(globalItr, InputManagerComp);

  // Register all gizmos that where active in the last frame.
  geo_query_env_clear(gizmoComp->queryEnv);
  dynarray_for_t(&gizmoComp->entries, DebugGizmo, entry) { debug_gizmo_register(gizmoComp, entry); }
  dynarray_clear(&gizmoComp->entries);

  // Test which gizmo is being hovered.
  EcsView* cameraView = ecs_world_view_t(world, CameraView);
  if (ecs_view_contains(cameraView, input_active_window(input))) {
    EcsIterator*              camItr      = ecs_view_at(cameraView, input_active_window(input));
    const SceneCameraComp*    camera      = ecs_view_read_t(camItr, SceneCameraComp);
    const SceneTransformComp* cameraTrans = ecs_view_read_t(camItr, SceneTransformComp);

    const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
    const f32       inputAspect  = input_cursor_aspect(input);
    const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);

    GeoQueryRayHit hit;
    gizmoComp->isHovering = geo_query_ray(gizmoComp->queryEnv, &inputRay, &hit);
    gizmoComp->activeId   = gizmoComp->isHovering ? (DebugGizmoId)hit.shapeId : 0;
  }
}

void debug_gizmo_draw_translation(
    const DebugGizmoComp* comp, DebugShapeComp* shape, const DebugGizmo* gizmo) {
  diag_assert(gizmo->type == DebugGizmoType_Translation);
  const bool isHovered = comp->isHovering && comp->activeId == gizmo->id;

  // Draw all translation arrows.
  const GeoVector pos = gizmo->data_translation.pos;
  const GeoQuat   rot = gizmo->data_translation.rot;
  for (DebugGizmoAxis a = 0; a != DebugGizmoAxis_Count; ++a) {
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

  ecs_register_system(DebugGizmoRenderSys, ecs_view_id(GlobalRenderView));

  ecs_order(DebugGizmoUpdateSys, DebugOrder_GizmoUpdate);
  ecs_order(DebugGizmoRenderSys, DebugOrder_GizmoRender);
}

DebugGizmoId debug_gizmo_id_entity(const EcsEntityId entity) {
  return (DebugGizmoId)ecs_entity_id_index(entity);
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
