#include "core_alloc.h"
#include "core_dynarray.h"
#include "ecs_world.h"
#include "scene_debug.h"
#include "scene_register.h"

ecs_comp_define(SceneDebugComp) {
  Allocator* allocTransient;
  DynArray   data; // SceneDebug[].
};

static void ecs_destruct_debug(void* data) {
  SceneDebugComp* comp = data;
  if (comp->allocTransient) {
    alloc_chunked_destroy(comp->allocTransient);
  }
  dynarray_destroy(&comp->data);
}

static void ecs_combine_debug(void* dataA, void* dataB) {
  (void)dataA;
  (void)dataB;
  // NOTE: No need to copy the entries as all entries are cleared at the next frame anyway.
}

static Mem debug_transient_dup(SceneDebugComp* comp, const Mem mem, const usize align) {
  if (!comp->allocTransient) {
    const usize chunkSize = 4 * usize_kibibyte;
    comp->allocTransient  = alloc_chunked_create(g_allocHeap, alloc_bump_create, chunkSize);
  }
  return alloc_dup(comp->allocTransient, mem, align);
}

ecs_view_define(DebugEntryView) { ecs_access_write(SceneDebugComp); }

ecs_system_define(SceneDebugInitSys) {
  EcsView* entryView = ecs_world_view_t(world, DebugEntryView);
  for (EcsIterator* itr = ecs_view_itr(entryView); ecs_view_walk(itr);) {
    SceneDebugComp* comp = ecs_view_write_t(itr, SceneDebugComp);

    // Clear last frame's data.
    if (comp->allocTransient) {
      alloc_reset(comp->allocTransient);
    }
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

void scene_debug_line(SceneDebugComp* comp, const SceneDebugLine params) {
  *dynarray_push_t(&comp->data, SceneDebug) = (SceneDebug){
      .type      = SceneDebugType_Line,
      .data_line = params,
  };
}

void scene_debug_sphere(SceneDebugComp* comp, const SceneDebugSphere params) {
  *dynarray_push_t(&comp->data, SceneDebug) = (SceneDebug){
      .type        = SceneDebugType_Sphere,
      .data_sphere = params,
  };
}

void scene_debug_box(SceneDebugComp* comp, const SceneDebugBox params) {
  *dynarray_push_t(&comp->data, SceneDebug) = (SceneDebug){
      .type     = SceneDebugType_Box,
      .data_box = params,
  };
}

void scene_debug_array(SceneDebugComp* comp, const SceneDebugArrow params) {
  *dynarray_push_t(&comp->data, SceneDebug) = (SceneDebug){
      .type       = SceneDebugType_Arrow,
      .data_arrow = params,
  };
}

void scene_debug_orientation(SceneDebugComp* comp, const SceneDebugOrientation params) {
  *dynarray_push_t(&comp->data, SceneDebug) = (SceneDebug){
      .type             = SceneDebugType_Orientation,
      .data_orientation = params,
  };
}

void scene_debug_text(SceneDebugComp* comp, const SceneDebugText params) {
  if (!params.text.size) {
    return;
  }
  const Mem textDup = debug_transient_dup(comp, params.text, 1);
  if (!mem_valid(textDup)) {
    // TODO: Report error.
    return; // Transient allocator ran out of space.
  }
  *dynarray_push_t(&comp->data, SceneDebug) = (SceneDebug){
      .type = SceneDebugType_Text,
      .data_text =
          {
              .pos      = params.pos,
              .color    = params.color,
              .text     = textDup,
              .fontSize = params.fontSize,
          },
  };
}

void scene_debug_trace(SceneDebugComp* comp, const SceneDebugTrace params) {
  if (!params.text.size) {
    return;
  }
  const Mem textDup = debug_transient_dup(comp, params.text, 1);
  if (!mem_valid(textDup)) {
    // TODO: Report error.
    return; // Transient allocator ran out of space.
  }
  *dynarray_push_t(&comp->data, SceneDebug) = (SceneDebug){
      .type       = SceneDebugType_Trace,
      .data_trace = {.text = textDup},
  };
}

SceneDebugComp* scene_debug_init(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world, entity, SceneDebugComp, .data = dynarray_create_t(g_allocHeap, SceneDebug, 0));
}

const SceneDebug* scene_debug_data(const SceneDebugComp* comp) {
  return dynarray_begin_t(&comp->data, SceneDebug);
}

usize scene_debug_count(const SceneDebugComp* comp) { return comp->data.size; }
