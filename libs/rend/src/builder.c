#include "core_alloc.h"
#include "core_dynarray.h"

#include "builder_internal.h"
#include "rvk/pass_internal.h"

struct sRendBuilder {
  Allocator* allocator;
  DynArray   drawBuffer; // RvkPassDraw[]
};

RendBuilder* rend_builder_create(Allocator* alloc) {
  RendBuilder* builder = alloc_alloc_t(alloc, RendBuilder);

  *builder = (RendBuilder){
      .allocator  = alloc,
      .drawBuffer = dynarray_create_t(alloc, RvkPassDraw, 8),
  };

  return builder;
}

void rend_builder_destroy(RendBuilder* builder) {
  dynarray_destroy(&builder->drawBuffer);

  alloc_free_t(builder->allocator, builder);
}
