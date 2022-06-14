#include "core_alloc.h"
#include "core_dynarray.h"
#include "debug_register.h"
#include "debug_text.h"
#include "ecs_world.h"
#include "log_logger.h"

#define debug_text_transient_chunk_size (4 * usize_kibibyte)
#define debug_text_transient_max 512

typedef struct {
  GeoVector pos;
  String    text;
} DebugText3D;

ecs_comp_define(DebugTextComp) {
  DynArray   entries; // DebugText3D[]
  Allocator* allocTransient;
};

static void ecs_destruct_text(void* data) {
  DebugTextComp* comp = data;
  dynarray_destroy(&comp->entries);
  alloc_chunked_destroy(comp->allocTransient);
}

ecs_view_define(TextView) { ecs_access_write(DebugTextComp); }

ecs_system_define(DebugTextRenderSys) {
  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, TextView)); ecs_view_walk(itr);) {
    DebugTextComp* textComp = ecs_view_write_t(itr, DebugTextComp);

    dynarray_clear(&textComp->entries);
  }
}

ecs_module_init(debug_text_module) {
  ecs_register_comp(DebugTextComp, .destructor = ecs_destruct_text);

  ecs_register_view(TextView);

  ecs_register_system(DebugTextRenderSys, ecs_view_id(TextView));

  ecs_order(DebugTextRenderSys, DebugOrder_TextRender);
}

DebugTextComp* debug_text_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world,
      entity,
      DebugTextComp,
      .entries = dynarray_create_t(g_alloc_heap, DebugText3D, 64),
      .allocTransient =
          alloc_chunked_create(g_alloc_page, alloc_bump_create, debug_text_transient_chunk_size));
}

void debug_text(DebugTextComp* comp, const GeoVector pos, const String text) {
  if (UNLIKELY(text.size > debug_text_transient_max)) {
    log_e(
        "Debug text size exceeds maximum",
        log_param("size", fmt_size(text.size)),
        log_param("limit", fmt_size(debug_text_transient_max)));
    return;
  }
  if (UNLIKELY(!text.size)) {
    return;
  }
  // TODO: Report error when the transient allocator runs out of space.
  *dynarray_push_t(&comp->entries, DebugText3D) = (DebugText3D){
      .pos  = pos,
      .text = string_dup(comp->allocTransient, text),
  };
}
