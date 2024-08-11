#include "core_diag.h"
#include "trace_tracer.h"

#include "def_internal.h"
#include "finalizer_internal.h"

// #define VOLO_ECS_TRACE_DESTRUCTORS

static i8 ecs_destruct_compare_entry(const void* a, const void* b) {
  return compare_i32(
      field_ptr(a, EcsFinalizerEntry, destructOrder),
      field_ptr(b, EcsFinalizerEntry, destructOrder));
}

EcsFinalizer ecs_finalizer_create(Allocator* alloc, const EcsDef* def) {
  return (EcsFinalizer){
      .def     = def,
      .entries = dynarray_create_t(alloc, EcsFinalizerEntry, 64),
  };
}

void ecs_finalizer_destroy(EcsFinalizer* finalizer) {
  diag_assert_msg(finalizer->entries.size == 0, "Finalizer cannot be destroyed with pending items");
  dynarray_destroy(&finalizer->entries);
}

void ecs_finalizer_push(EcsFinalizer* finalizer, const EcsCompId compId, void* compData) {
  EcsCompDestructor destructor = ecs_def_comp_destructor(finalizer->def, compId);
  if (LIKELY(!destructor)) {
    return;
  }
  *dynarray_push_t(&finalizer->entries, EcsFinalizerEntry) = (EcsFinalizerEntry){
      .destructor    = destructor,
      .destructOrder = ecs_def_comp_destruct_order(finalizer->def, compId),
      .compId        = compId,
      .compData      = compData,
  };
}

void ecs_finalizer_flush(EcsFinalizer* finalizer) {
  // Sort entries on their destruct-order.
  dynarray_sort(&finalizer->entries, ecs_destruct_compare_entry);

  // Execute the destructs.
  dynarray_for_t(&finalizer->entries, EcsFinalizerEntry, entry) {
#ifdef VOLO_ECS_TRACE_DESTRUCTORS
    MAYBE_UNUSED const String compName = ecs_def_comp_name(finalizer->def, entry->compId);
    trace_begin_msg("ecs_comp_destruct", TraceColor_Red, "destruct_{}", fmt_text(compName));
#endif

    entry->destructor(entry->compData);

#ifdef VOLO_ECS_TRACE_DESTRUCTORS
    trace_end();
#endif
  }

  // Clear the finalizer.
  dynarray_clear(&finalizer->entries);
}
