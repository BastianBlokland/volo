#include "core_alloc.h"
#include "core_dynarray.h"
#include "debug_gizmo.h"
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

ecs_module_init(debug_gizmo_module) {
  ecs_register_comp(DebugGizmoComp, .destructor = ecs_destruct_gizmo);
}

DebugGizmoComp* debug_gimzo_create(EcsWorld* world, const EcsEntityId entity) {
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
