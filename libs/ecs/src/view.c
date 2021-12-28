#include "core_alloc.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "ecs_def.h"

#include "storage_internal.h"
#include "view_internal.h"

static usize ecs_view_bytes_per_mask(const EcsDef* def) {
  const usize compCount = ecs_def_comp_count(def);
  return bits_to_bytes(compCount) + 1;
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
  const BitSet mask = ecs_view_mask(view, EcsViewMask_AccessRead);
  EcsIterator* itr  = ecs_iterator_create_with_count(mem, mask, view->compCount);
  itr->context      = view;
  return itr;
}

EcsIterator* ecs_view_itr_reset(EcsIterator* itr) {
  ecs_iterator_reset(itr);
  return itr;
}

EcsIterator* ecs_view_walk(EcsIterator* itr) {
  EcsView* view = itr->context;

  if (UNLIKELY(itr->archetypeIdx >= view->archetypes.size)) {
    return null;
  }

  const EcsArchetypeId id = *dynarray_at_t(&view->archetypes, itr->archetypeIdx, EcsArchetypeId);
  if (LIKELY(ecs_storage_itr_walk(view->storage, itr, id))) {
    return itr;
  }

  ++itr->archetypeIdx;
  return ecs_view_walk(itr);
}

EcsIterator* ecs_view_jump(EcsIterator* itr, const EcsEntityId entity) {
  EcsView* view = itr->context;

  diag_assert_msg(
      ecs_view_contains(view, entity),
      "View {} does not contain entity {}",
      fmt_text(view->viewDef->name),
      fmt_int(entity));

  ecs_storage_itr_jump(view->storage, itr, entity);
  return itr;
}

EcsIterator* ecs_view_maybe_jump(EcsIterator* itr, const EcsEntityId entity) {
  EcsView* view = itr->context;
  if (!ecs_view_contains(view, entity)) {
    return null;
  }
  ecs_storage_itr_jump(view->storage, itr, entity);
  return itr;
}

EcsEntityId ecs_view_entity(const EcsIterator* itr) {
  diag_assert_msg(itr->entity, "Iterator has not been initialized");
  return *itr->entity;
}

const void* ecs_view_read(const EcsIterator* itr, const EcsCompId comp) {
  diag_assert_msg(itr && itr->entity, "Iterator has not been initialized");

  EcsView* view = itr->context;

  diag_assert_msg(
      bitset_test(ecs_view_mask(view, EcsViewMask_AccessRead), comp),
      "View {} does not have read-access to component {}",
      fmt_text(view->viewDef->name),
      fmt_text(ecs_def_comp_name(view->def, comp)));

  return ecs_iterator_access(itr, comp).ptr;
}

void* ecs_view_write(const EcsIterator* itr, const EcsCompId comp) {
  diag_assert_msg(itr && itr->entity, "Iterator has not been initialized");

  EcsView* view = itr->context;

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
  Mem         masksMem     = alloc_alloc(alloc, bytesPerMask * 4, 1);
  mem_set(masksMem, 0);

  EcsView view = {
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

  view.compCount = bitset_count(ecs_view_mask(&view, EcsViewMask_AccessRead));
  return view;
}

void ecs_view_destroy(Allocator* alloc, const EcsDef* def, EcsView* view) {
  alloc_free(alloc, mem_create(view->masks.ptr, ecs_view_bytes_per_mask(def) * 4));
  dynarray_destroy(&view->archetypes);
}

BitSet ecs_view_mask(const EcsView* view, EcsViewMaskType type) {
  const usize bytesPerMask = ecs_view_bytes_per_mask(view->def);
  return mem_slice(view->masks, bytesPerMask * type, ecs_view_bytes_per_mask(view->def));
}

bool ecs_view_conflict(const EcsView* a, const EcsView* b) {
  /**
   * Check if 'A' conflicts with 'B'.
   * Meaning 'A' reads a component that 'B' writes or writes a component that the 'B' reads.
   */
  const BitSet aRequired = ecs_view_mask(a, EcsViewMask_FilterWith);
  const BitSet bRequired = ecs_view_mask(b, EcsViewMask_FilterWith);

  if (bitset_any_of(aRequired, ecs_view_mask(b, EcsViewMask_FilterWithout))) {
    return false; // 'A' requires something that 'B' excludes; no conflict.
  }
  if (bitset_any_of(bRequired, ecs_view_mask(a, EcsViewMask_FilterWithout))) {
    return false; // 'B' requires something that 'A' excludes; no conflict.
  }

  const BitSet aReads  = ecs_view_mask(a, EcsViewMask_AccessRead);
  const BitSet aWrites = ecs_view_mask(a, EcsViewMask_AccessWrite);

  if (bitset_any_of(aReads, ecs_view_mask(b, EcsViewMask_AccessWrite))) {
    return true; // 'A' reads something that 'B' writes; conflict.
  }
  if (bitset_any_of(aWrites, ecs_view_mask(b, EcsViewMask_AccessRead))) {
    return true; // 'A' writes something the 'B' reads; conflict.
  }

  return false; // No conflict.
}

bool ecs_view_maybe_track(EcsView* view, const EcsArchetypeId id, const BitSet mask) {
  if (ecs_view_matches(view, mask)) {
    *dynarray_insert_sorted_t(&view->archetypes, EcsArchetypeId, ecs_compare_archetype, &id) = id;
    return true;
  }
  return false;
}
