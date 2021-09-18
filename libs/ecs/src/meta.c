#include "core_alloc.h"

#include "meta_internal.h"

EcsMeta* ecs_meta_create(Allocator* alloc) {
  EcsMeta* meta = alloc_alloc_t(alloc, EcsMeta);
  *meta         = (EcsMeta){
      .components = dynarray_create_t(alloc, EcsCompMeta, 256),
      .alloc      = alloc,
  };
  return meta;
}

void ecs_meta_destroy(EcsMeta* meta) {
  dynarray_for_t(&meta->components, EcsCompMeta, comp, { string_free(meta->alloc, comp->name); });
  dynarray_destroy(&meta->components);

  alloc_free_t(meta->alloc, meta);
}

EcsCompId
ecs_register_comp_id(EcsMeta* meta, const String name, const usize size, const usize align) {
  EcsCompId id                                     = (EcsCompId)meta->components.size;
  *dynarray_push_t(&meta->components, EcsCompMeta) = (EcsCompMeta){
      .name  = string_dup(meta->alloc, name),
      .size  = size,
      .align = align,
  };
  return id;
}

EcsCompMeta* ecs_comp_meta(EcsMeta* meta, EcsCompId id) {
  return dynarray_at_t(&meta->components, (usize)id, EcsCompMeta);
}

String ecs_comp_name(EcsMeta* meta, const EcsCompId id) { return ecs_comp_meta(meta, id)->name; }
