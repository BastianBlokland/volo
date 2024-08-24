#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "jobs_executor.h"

#include "builder_internal.h"
#include "rvk/pass_internal.h"

#define rend_builder_workers_max 8

struct sRendBuilderBuffer {
  RvkPass*    pass;
  RvkGraphic* drawGraphic;
  RvkMesh*    drawMesh;
  RvkImage*   drawImage;
  DynArray    draws; // RvkPassDraw[]
};

struct sRendBuilder {
  Allocator*        allocator;
  RendBuilderBuffer buffers[rend_builder_workers_max];
};

RendBuilder* rend_builder_create(Allocator* alloc) {
  RendBuilder* builder = alloc_alloc_t(alloc, RendBuilder);

  *builder = (RendBuilder){.allocator = alloc};

  for (u32 i = 0; i != rend_builder_workers_max; ++i) {
    builder->buffers[i].draws = dynarray_create_t(alloc, RvkPassDraw, 8);
  }

  return builder;
}

void rend_builder_destroy(RendBuilder* builder) {
  for (u32 i = 0; i != rend_builder_workers_max; ++i) {
    dynarray_destroy(&builder->buffers[i].draws);
  }
  alloc_free_t(builder->allocator, builder);
}

RendBuilderBuffer* rend_builder_buffer(const RendBuilder* builder) {
  diag_assert(g_jobsWorkerId < rend_builder_workers_max);
  return (RendBuilderBuffer*)&builder->buffers[g_jobsWorkerId];
}

void rend_builder_clear(RendBuilderBuffer* buffer) {
  buffer->pass        = null;
  buffer->drawGraphic = null;
  buffer->drawMesh    = null;
  buffer->drawImage   = null;
  dynarray_clear(&buffer->draws);
}

void rend_builder_set_pass(RendBuilderBuffer* buffer, RvkPass* pass) {
  diag_assert_msg(!buffer->pass, "Pass already set");
  buffer->pass = pass;
}

void rend_builder_set_draw_graphic(RendBuilderBuffer* buffer, RvkGraphic* graphic) {
  diag_assert_msg(!buffer->drawGraphic, "Draw graphic already set");
  buffer->drawGraphic = graphic;
}

void rend_builder_set_draw_mesh(RendBuilderBuffer* buffer, RvkMesh* mesh) {
  diag_assert_msg(!buffer->drawMesh, "Draw mesh already set");
  buffer->drawMesh = mesh;
}

void rend_builder_set_draw_image(RendBuilderBuffer* buffer, RvkImage* image) {
  diag_assert_msg(!buffer->drawImage, "Draw image already set");
  buffer->drawImage = image;
}
