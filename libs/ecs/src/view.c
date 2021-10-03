#include "core_alloc.h"
#include "core_diag.h"
#include "core_memory.h"
#include "ecs_def.h"

#include "view_internal.h"

static usize ecs_view_bytes_per_mask(const EcsDef* def) {
  const usize compCount = ecs_def_comp_count(def);
  return bits_to_bytes(compCount) + 1;
}

EcsView ecs_view_create(Allocator* alloc, const EcsDef* def, const EcsViewDef* viewDef) {
  diag_assert(alloc && def);

  const usize bytesPerMask = ecs_view_bytes_per_mask(def);
  Mem         masksMem     = alloc_alloc(alloc, bytesPerMask * 4, 1);

  EcsView view = (EcsView){
      .filterWith    = mem_slice(masksMem, bytesPerMask * 0, bytesPerMask),
      .filterWithout = mem_slice(masksMem, bytesPerMask * 1, bytesPerMask),
      .accessRead    = mem_slice(masksMem, bytesPerMask * 2, bytesPerMask),
      .accessWrite   = mem_slice(masksMem, bytesPerMask * 3, bytesPerMask),
      .archetypes    = dynarray_create_t(alloc, EcsArchetypeId, 128),
  };

  EcsViewBuilder viewBuilder = {
      .def           = def,
      .filterWith    = view.filterWith,
      .filterWithout = view.filterWithout,
      .accessRead    = view.accessRead,
      .accessWrite   = view.accessWrite,
  };

  viewDef->initRoutine(&viewBuilder);
  return view;
}

void ecs_view_destroy(Allocator* alloc, const EcsDef* def, EcsView* view) {
  alloc_free(alloc, mem_create(view->filterWith.ptr, ecs_view_bytes_per_mask(def) * 4));
  dynarray_destroy(&view->archetypes);
}

bool ecs_view_matches(const EcsView* view, BitSet mask) {
  return bitset_all_of(mask, view->filterWith) && !bitset_any_of(mask, view->filterWithout);
}

bool ecs_view_maybe_track(EcsView* view, const EcsArchetypeId id, BitSet mask) {
  if (ecs_view_matches(view, mask)) {
    *dynarray_push_t(&view->archetypes, EcsArchetypeId) = id;
    return true;
  }
  return false;
}
