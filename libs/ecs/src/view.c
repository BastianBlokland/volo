#include "core_alloc.h"
#include "core_diag.h"
#include "core_memory.h"
#include "ecs_def.h"

#include "view_internal.h"

static usize ecs_view_bytes_per_mask(const EcsDef* def) {
  const usize compCount = ecs_def_comp_count(def);
  return bits_to_bytes(compCount) + 1;
}

static void* ecs_view_comp(EcsView* view, const EcsEntityId entity, const EcsCompId comp) {

  diag_assert_msg(
      ecs_view_contains(view, entity),
      "View {} does not contain entity {}",
      fmt_text(view->viewDef->name),
      fmt_int(entity));

  return ecs_storage_entity_comp(view->storage, entity, comp);
}

EcsView ecs_view_create(
    Allocator* alloc, EcsStorage* storage, const EcsDef* def, const EcsViewDef* viewDef) {
  diag_assert(alloc && def);

  const usize bytesPerMask = ecs_view_bytes_per_mask(def);
  Mem         masksMem     = alloc_alloc(alloc, bytesPerMask * 4, 1);
  mem_set(masksMem, 0);

  EcsView view = (EcsView){
      .def           = def,
      .viewDef       = viewDef,
      .storage       = storage,
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

bool ecs_view_maybe_track(EcsView* view, const EcsArchetypeId id, const BitSet mask) {
  if (ecs_view_matches(view, mask)) {
    *dynarray_push_t(&view->archetypes, EcsArchetypeId) = id;
    return true;
  }
  return false;
}

bool ecs_view_contains(EcsView* view, const EcsEntityId entity) {
  const BitSet entityMask = ecs_storage_entity_mask(view->storage, entity);
  return ecs_view_matches(view, entityMask);
}

const void* ecs_view_comp_read(EcsView* view, const EcsEntityId entity, const EcsCompId comp) {
  diag_assert_msg(
      bitset_test(view->accessRead, comp),
      "View {} does not have read-access to component {}",
      fmt_text(view->viewDef->name),
      fmt_text(ecs_def_comp_name(view->def, comp)));

  return ecs_view_comp(view, entity, comp);
}

void* ecs_view_comp_write(EcsView* view, const EcsEntityId entity, const EcsCompId comp) {
  diag_assert_msg(
      bitset_test(view->accessWrite, comp),
      "View {} does not have write-access to component {}",
      fmt_text(view->viewDef->name),
      fmt_text(ecs_def_comp_name(view->def, comp)));

  return ecs_view_comp(view, entity, comp);
}
