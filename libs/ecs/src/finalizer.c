#include "def_internal.h"
#include "finalizer_internal.h"

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

void ecs_finalizer_destroy(EcsFinalizer* finalizer) { dynarray_destroy(&finalizer->entries); }

void ecs_finalizer_push(EcsFinalizer* finalizer, const EcsCompId compId, void* compData) {
  EcsCompDestructor destructor = ecs_def_comp_destructor(finalizer->def, compId);
  if (LIKELY(!destructor)) {
    return;
  }
  *dynarray_push_t(&finalizer->entries, EcsFinalizerEntry) = (EcsFinalizerEntry){
      .destructor    = destructor,
      .destructOrder = ecs_def_comp_destruct_order(finalizer->def, compId),
      .compData      = compData,
  };
}

void ecs_finalizer_flush(EcsFinalizer* finalizer) {
  // Sort entries on their destruct-order.
  dynarray_sort(&finalizer->entries, ecs_destruct_compare_entry);

  // Execute the destructs.
  dynarray_for_t(&finalizer->entries, EcsFinalizerEntry, entry) {
    entry->destructor(entry->compData);
  }

  // Clear the finalizer.
  dynarray_clear(&finalizer->entries);
}
