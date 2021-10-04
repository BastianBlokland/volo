#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_def.h"

#include "storage_internal.h"
#include "view_internal.h"

typedef enum {
  EcsViewMask_FilterWith,
  EcsViewMask_FilterWithout,
  EcsViewMask_AccessRead,
  EcsViewMask_AccessWrite,
  EcsViewMask_CompMask,
} EcsViewMaskType;

static usize ecs_view_bytes_per_mask(const EcsDef* def) {
  const usize compCount = ecs_def_comp_count(def);
  return bits_to_bytes(compCount) + 1;
}

static BitSet ecs_view_mask(const EcsView* view, EcsViewMaskType type) {
  const usize bytesPerMask = ecs_view_bytes_per_mask(view->def);
  return mem_slice(view->masks, bytesPerMask * type, ecs_view_bytes_per_mask(view->def));
}

static bool ecs_view_matches(const EcsView* view, BitSet mask) {
  return bitset_all_of(mask, ecs_view_mask(view, EcsViewMask_FilterWith)) &&
         !bitset_any_of(mask, ecs_view_mask(view, EcsViewMask_FilterWithout));
}

usize ecs_view_comp_count(EcsView* view) { return view->compCount; }

bool ecs_view_contains(EcsView* view, const EcsEntityId entity) {
  const EcsArchetypeId archetype = ecs_storage_entity_archetype(view->storage, entity);
  return dynarray_search_binary(&view->archetypes, ecs_compare_archetype, &archetype) != null;
}

EcsIterator* ecs_view_itr_create(Mem mem, EcsView* view) {
  const BitSet mask = ecs_view_mask(view, EcsViewMask_CompMask);
  EcsIterator* itr  = ecs_iterator_create_with_count(mem, mask, view->compCount);
  itr->context      = view;
  return itr;
}

bool ecs_view_itr_walk(EcsIterator* itr) {
  EcsView* view = itr->context;

  if (UNLIKELY(itr->archetypeIdx >= view->archetypes.size)) {
    return false;
  }

  const EcsArchetypeId id = *dynarray_at_t(&view->archetypes, itr->archetypeIdx, EcsArchetypeId);
  if (LIKELY(ecs_storage_itr_walk(view->storage, itr, id))) {
    return true;
  }

  ++itr->archetypeIdx;
  return ecs_view_itr_walk(itr);
}

void ecs_view_itr_jump(EcsIterator* itr, const EcsEntityId entity) {
  EcsView* view = itr->context;

  diag_assert_msg(
      ecs_view_contains(view, entity),
      "View {} does not contain entity {}",
      fmt_text(view->viewDef->name),
      fmt_int(entity));

  ecs_storage_itr_jump(view->storage, itr, entity);
}

const void* ecs_view_read(EcsIterator* itr, const EcsCompId comp) {
  EcsView* view = itr->context;

  diag_assert_msg(itr->entity, "Iterator has not been initialized");

  diag_assert_msg(
      bitset_test(ecs_view_mask(view, EcsViewMask_AccessRead), comp),
      "View {} does not have read-access to component {}",
      fmt_text(view->viewDef->name),
      fmt_text(ecs_def_comp_name(view->def, comp)));

  return ecs_iterator_access(itr, comp).ptr;
}

void* ecs_view_write(EcsIterator* itr, const EcsCompId comp) {
  EcsView* view = itr->context;

  diag_assert_msg(itr->entity, "Iterator has not been initialized");

  diag_assert_msg(
      bitset_test(ecs_view_mask(view, EcsViewMask_AccessWrite), comp),
      "View {} does not have write-access to component {}",
      fmt_text(view->viewDef->name),
      fmt_text(ecs_def_comp_name(view->def, comp)));

  return ecs_iterator_access(itr, comp).ptr;
}

EcsView ecs_view_create(
    Allocator* alloc, EcsStorage* storage, const EcsDef* def, const EcsViewDef* viewDef) {
  diag_assert(alloc && def);

  const usize bytesPerMask = ecs_view_bytes_per_mask(def);
  Mem         masksMem     = alloc_alloc(alloc, bytesPerMask * 5, 1);
  mem_set(masksMem, 0);

  EcsView view = (EcsView){
      .def        = def,
      .viewDef    = viewDef,
      .storage    = storage,
      .masks      = masksMem,
      .archetypes = dynarray_create_t(alloc, EcsArchetypeId, 128),
  };

  EcsViewBuilder viewBuilder = {
      .def           = def,
      .filterWith    = ecs_view_mask(&view, EcsViewMask_FilterWith),
      .filterWithout = ecs_view_mask(&view, EcsViewMask_FilterWithout),
      .accessRead    = ecs_view_mask(&view, EcsViewMask_AccessRead),
      .accessWrite   = ecs_view_mask(&view, EcsViewMask_AccessWrite),
  };

  viewDef->initRoutine(&viewBuilder);

  BitSet compMask = ecs_view_mask(&view, EcsViewMask_CompMask);
  bitset_or(compMask, ecs_view_mask(&view, EcsViewMask_AccessRead));
  bitset_or(compMask, ecs_view_mask(&view, EcsViewMask_AccessWrite));
  bitset_and(compMask, ecs_view_mask(&view, EcsViewMask_FilterWith));

  view.compCount = bitset_count(compMask);
  return view;
}

void ecs_view_destroy(Allocator* alloc, const EcsDef* def, EcsView* view) {
  alloc_free(alloc, mem_create(view->masks.ptr, ecs_view_bytes_per_mask(def) * 5));
  dynarray_destroy(&view->archetypes);
}

bool ecs_view_maybe_track(EcsView* view, const EcsArchetypeId id, const BitSet mask) {
  if (ecs_view_matches(view, mask)) {
    *dynarray_insert_sorted_t(&view->archetypes, EcsArchetypeId, ecs_compare_archetype, &id) = id;
    return true;
  }
  return false;
}
