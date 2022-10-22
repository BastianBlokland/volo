#include "core_alloc.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_def.h"
#include "ecs_runner.h"

#include "storage_internal.h"
#include "view_internal.h"

MAYBE_UNUSED static bool ecs_iterator_is_stepped(EcsIterator* iterator) {
  return iterator->chunksToSkip || !sentinel_check(iterator->chunksLimitRemaining);
}

static bool ecs_view_matches(const EcsView* view, BitSet mask) {
  return ecs_comp_mask_all_of(mask, ecs_view_mask(view, EcsViewMask_FilterWith)) &&
         !ecs_comp_mask_any_of(mask, ecs_view_mask(view, EcsViewMask_FilterWithout));
}

MAYBE_UNUSED static void ecs_view_validate_random_write(const EcsView* view, const EcsSystemId id) {
  if (view->flags & EcsViewFlags_AllowParallelRandomWrite) {
    return; // View explicitly allows random parallel writes.
  }

  const EcsSystemDef* sysDef = dynarray_at_t(&view->def->systems, id, EcsSystemDef);
  (void)sysDef;

  diag_assert_msg(
      sysDef->parallelCount <= 1,
      "Parallel system '{}' creates a random-write iterator from view '{}', "
      "this is potentially unsafe",
      fmt_text(sysDef->name),
      fmt_text(view->viewDef->name));
}

u16 ecs_view_comp_count(const EcsView* view) { return view->compCount; }

FLATTEN_HINT bool ecs_view_contains(const EcsView* view, const EcsEntityId entity) {
  const EcsArchetypeId archetype = ecs_storage_entity_archetype(view->storage, entity);
  dynarray_for_t(&view->archetypes, EcsArchetypeId, trackedArchetype) {
    if (*trackedArchetype == archetype) {
      return true;
    }
    if (*trackedArchetype > archetype) {
      return false;
    }
  }
  return false;
}

EcsIterator* ecs_view_itr_create(Mem mem, EcsView* view) {
  const BitSet mask = ecs_view_mask(view, EcsViewMask_AccessRead);
  EcsIterator* itr  = ecs_iterator_create_with_count(mem, mask, view->compCount);
  itr->context      = view;

#ifndef VOLO_FAST
  if (g_ecsRunningSystem && bitset_any(ecs_view_mask(view, EcsViewMask_AccessWrite))) {
    ecs_view_validate_random_write(view, g_ecsRunningSystemId);
  }
#endif

  return itr;
}

EcsIterator* ecs_view_itr_step_create(Mem mem, EcsView* view, const u16 steps, const u16 index) {
  diag_assert_msg(steps, "Stepped iterator needs at least 1 step");
  diag_assert_msg(
      index < steps,
      "Index {} is invalid for stepped iterator with {} steps",
      fmt_int(index),
      fmt_int(steps));

  const BitSet mask = ecs_view_mask(view, EcsViewMask_AccessRead);
  EcsIterator* itr  = ecs_iterator_create_with_count(mem, mask, view->compCount);
  itr->context      = view;

  const u32 totalChunks   = ecs_view_chunks(view);
  const u32 chunksPerStep = math_max(1, (u32)math_round_nearest_f32(totalChunks / (f32)steps));
  const u32 chunksToSkip  = index * chunksPerStep;

  diag_assert(chunksPerStep <= u16_max);
  diag_assert(chunksToSkip <= u16_max);

  itr->chunksToSkip = (u16)chunksToSkip;
  if (index != (steps - 1)) {
    // Not the last step; limit the amount of chunks. On the last step process all remaining chunks.
    itr->chunksLimitRemaining = (u16)chunksPerStep;
  }
  return itr;
}

FLATTEN_HINT EcsIterator* ecs_view_itr_reset(EcsIterator* itr) {
  diag_assert_msg(!ecs_iterator_is_stepped(itr), "Stepped iterators cannot be reset");

  ecs_iterator_reset(itr);
  return itr;
}

FLATTEN_HINT EcsIterator* ecs_view_walk(EcsIterator* itr) {
  EcsView* view = itr->context;

  if (UNLIKELY(itr->archetypeIdx >= view->archetypes.size)) {
    return null;
  }

  const u16            archIdx = itr->archetypeIdx;
  const EcsArchetypeId id      = *(dynarray_begin_t(&view->archetypes, EcsArchetypeId) + archIdx);
  if (LIKELY(ecs_storage_itr_walk(view->storage, itr, id))) {
    return itr;
  }

  if (!itr->chunksLimitRemaining) {
    return null; // No more chunks allowed to process.
  }

  ++itr->archetypeIdx;
  return ecs_view_walk(itr);
}

FLATTEN_HINT EcsIterator* ecs_view_jump(EcsIterator* itr, const EcsEntityId entity) {
  diag_assert_msg(!ecs_iterator_is_stepped(itr), "Stepped iterators cannot be jumped");

  EcsView* view = itr->context;

  diag_assert_msg(
      ecs_view_contains(view, entity),
      "View {} does not contain entity {}",
      fmt_text(view->viewDef->name),
      fmt_int(entity, .base = 16));

  ecs_storage_itr_jump(view->storage, itr, entity);
  return itr;
}

FLATTEN_HINT EcsIterator* ecs_view_maybe_jump(EcsIterator* itr, const EcsEntityId entity) {
  diag_assert_msg(!ecs_iterator_is_stepped(itr), "Stepped iterators cannot be jumped");

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
  diag_assert_msg(itr->entity, "Iterator has not been initialized");

  MAYBE_UNUSED EcsView* view = itr->context;

  diag_assert_msg(
      ecs_comp_has(ecs_view_mask(view, EcsViewMask_AccessRead), comp),
      "View {} does not have read-access to component {}",
      fmt_text(view->viewDef->name),
      fmt_text(ecs_def_comp_name(view->def, comp)));

  return ecs_iterator_access(itr, comp).ptr;
}

void* ecs_view_write(const EcsIterator* itr, const EcsCompId comp) {
  diag_assert_msg(itr->entity, "Iterator has not been initialized");

  MAYBE_UNUSED EcsView* view = itr->context;

  diag_assert_msg(
      ecs_comp_has(ecs_view_mask(view, EcsViewMask_AccessWrite), comp),
      "View {} does not have write-access to component {}",
      fmt_text(view->viewDef->name),
      fmt_text(ecs_def_comp_name(view->def, comp)));

  return ecs_iterator_access(itr, comp).ptr;
}

u32 ecs_view_chunks(const EcsView* view) {
  u32 totalChunks = 0;
  dynarray_for_t(&view->archetypes, EcsArchetypeId, trackedArchetype) {
    totalChunks += ecs_storage_archetype_chunks_non_empty(view->storage, *trackedArchetype);
  }
  return totalChunks;
}

EcsView ecs_view_create(
    Allocator* alloc, EcsStorage* storage, const EcsDef* def, const EcsViewDef* viewDef) {
  diag_assert(alloc && def);

  const Mem masksMem = alloc_alloc(alloc, ecs_comp_mask_size(def) * 4, ecs_comp_mask_align);
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

  view.compCount = ecs_comp_mask_count(ecs_view_mask(&view, EcsViewMask_AccessRead));
  view.flags     = viewBuilder.flags;
  return view;
}

void ecs_view_destroy(Allocator* alloc, const EcsDef* def, EcsView* view) {
  alloc_free(alloc, mem_create(view->masks.ptr, ecs_comp_mask_size(def) * 4));
  dynarray_destroy(&view->archetypes);
}

BitSet ecs_view_mask(const EcsView* view, const EcsViewMaskType type) {
  const usize bytesPerMask = ecs_comp_mask_size(view->def);
  return mem_create(bits_ptr_offset(view->masks.ptr, bytesPerMask * type), bytesPerMask);
}

bool ecs_view_conflict(const EcsView* a, const EcsView* b) {
  /**
   * Check if 'A' conflicts with 'B'.
   * Meaning 'A' reads a component that 'B' writes or writes a component that the 'B' reads.
   */
  const BitSet aRequired = ecs_view_mask(a, EcsViewMask_FilterWith);
  const BitSet bRequired = ecs_view_mask(b, EcsViewMask_FilterWith);

  if (ecs_comp_mask_any_of(aRequired, ecs_view_mask(b, EcsViewMask_FilterWithout))) {
    return false; // 'A' requires something that 'B' excludes; no conflict.
  }
  if (ecs_comp_mask_any_of(bRequired, ecs_view_mask(a, EcsViewMask_FilterWithout))) {
    return false; // 'B' requires something that 'A' excludes; no conflict.
  }

  const BitSet aReads  = ecs_view_mask(a, EcsViewMask_AccessRead);
  const BitSet aWrites = ecs_view_mask(a, EcsViewMask_AccessWrite);

  if (ecs_comp_mask_any_of(aReads, ecs_view_mask(b, EcsViewMask_AccessWrite))) {
    return true; // 'A' reads something that 'B' writes; conflict.
  }
  if (ecs_comp_mask_any_of(aWrites, ecs_view_mask(b, EcsViewMask_AccessRead))) {
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
