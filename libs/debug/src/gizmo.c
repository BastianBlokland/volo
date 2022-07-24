#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "debug_gizmo.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "ecs_world.h"

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
  DynArray entries; // DebugGizmo[]
};

static void ecs_destruct_gizmo(void* data) {
  DebugGizmoComp* comp = data;
  dynarray_destroy(&comp->entries);
}

ecs_view_define(GlobalView) { ecs_access_write(DebugShapeComp); }
ecs_view_define(GizmoView) { ecs_access_write(DebugGizmoComp); }

void debug_gizmo_translation_draw(DebugShapeComp* shape, const DebugGizmoTranslation* gizmoT) {
  const f32       length  = 0.5f;
  const f32       radius  = 0.05f;
  const GeoVector right   = geo_quat_rotate(gizmoT->rot, geo_right);
  const GeoVector up      = geo_quat_rotate(gizmoT->rot, geo_up);
  const GeoVector forward = geo_quat_rotate(gizmoT->rot, geo_forward);

  debug_arrow(
      shape,
      gizmoT->pos,
      geo_vector_add(gizmoT->pos, geo_vector_mul(right, length)),
      radius,
      geo_color_red);

  debug_arrow(
      shape,
      gizmoT->pos,
      geo_vector_add(gizmoT->pos, geo_vector_mul(up, length)),
      radius,
      geo_color_green);

  debug_arrow(
      shape,
      gizmoT->pos,
      geo_vector_add(gizmoT->pos, geo_vector_mul(forward, length)),
      radius,
      geo_color_blue);

  debug_sphere(shape, gizmoT->pos, 0.025f, geo_color_black, DebugShape_Overlay);
}

ecs_system_define(DebugGizmoRenderSys) {
  // Create a global text component for convenience.
  if (!ecs_world_has_t(world, ecs_world_global(world), DebugGizmoComp)) {
    debug_gizmo_create(world, ecs_world_global(world));
  }

  // Query global dependencies.
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DebugShapeComp* shape = ecs_view_write_t(globalItr, DebugShapeComp);

  // Render all entries for all gizmo components.
  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, GizmoView)); ecs_view_walk(itr);) {
    DebugGizmoComp* gizmoComp = ecs_view_write_t(itr, DebugGizmoComp);
    dynarray_for_t(&gizmoComp->entries, DebugGizmo, entry) {
      switch (entry->type) {
      case DebugGizmoType_Translation:
        debug_gizmo_translation_draw(shape, &entry->data_translation);
        break;
      case DebugGizmoType_Count:
        diag_crash();
      }
    }
    dynarray_clear(&gizmoComp->entries);
  }
}

ecs_module_init(debug_gizmo_module) {
  ecs_register_comp(DebugGizmoComp, .destructor = ecs_destruct_gizmo);

  ecs_register_view(GlobalView);
  ecs_register_view(GizmoView);

  ecs_register_system(DebugGizmoRenderSys, ecs_view_id(GlobalView), ecs_view_id(GizmoView));

  ecs_order(DebugGizmoRenderSys, DebugOrder_GizmoRender);
}

DebugGizmoComp* debug_gizmo_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world, entity, DebugGizmoComp, .entries = dynarray_create_t(g_alloc_heap, DebugGizmo, 8));
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
