#include "core_alloc.h"
#include "core_dynarray.h"
#include "ecs_world.h"
#include "scene_debug.h"
#include "scene_register.h"

ecs_comp_define(SceneDebugComp) {
  DynArray data; // SceneDebug[].
};

static void ecs_destruct_debug(void* data) {
  SceneDebugComp* comp = data;
  dynarray_destroy(&comp->data);
}

static void ecs_combine_debug(void* dataA, void* dataB) {
  SceneDebugComp* a = dataA;
  SceneDebugComp* b = dataB;

  dynarray_for_t(&b->data, SceneDebug, entry) { scene_debug_push(a, *entry); }
  dynarray_destroy(&b->data);
}

ecs_view_define(DebugEntryView) { ecs_access_write(SceneDebugComp); }

ecs_system_define(SceneDebugInitSys) {
  EcsView* entryView = ecs_world_view_t(world, DebugEntryView);
  for (EcsIterator* itr = ecs_view_itr(entryView); ecs_view_walk(itr);) {
    SceneDebugComp* comp = ecs_view_write_t(itr, SceneDebugComp);

    // Clear last frame's data.
    dynarray_clear(&comp->data);
  }
}

ecs_module_init(scene_debug_module) {
  ecs_register_comp(
      SceneDebugComp, .destructor = ecs_destruct_debug, .combinator = ecs_combine_debug);

  ecs_register_view(DebugEntryView);

  ecs_register_system(SceneDebugInitSys, ecs_view_id(DebugEntryView));

  ecs_order(SceneDebugInitSys, SceneOrder_DebugInit);
}

SceneDebugComp* scene_debug_init(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world, entity, SceneDebugComp, .data = dynarray_create_t(g_allocHeap, SceneDebug, 0));
}

void scene_debug_push(SceneDebugComp* comp, const SceneDebug entry) {
  *dynarray_push_t(&comp->data, SceneDebug) = entry;
}

const SceneDebug* scene_debug_data(const SceneDebugComp* comp) {
  return dynarray_begin_t(&comp->data, SceneDebug);
}

usize scene_debug_count(const SceneDebugComp* comp) { return comp->data.size; }
