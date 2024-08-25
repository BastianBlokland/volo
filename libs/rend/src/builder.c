#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "jobs_executor.h"

#include "builder_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/pass_internal.h"

#define rend_builder_workers_max 8

struct sRendBuilderBuffer {
  ALIGNAS(64)
  RvkPass*    pass;
  RvkPassDraw stage;
  DynArray    draws; // RvkPassDraw[]
};

ASSERT(alignof(RendBuilderBuffer) == 64, "Unexpected buffer alignment")

struct sRendBuilder {
  Allocator*        allocator;
  RendBuilderBuffer buffers[rend_builder_workers_max];
};

static i8 builder_draw_compare(const void* a, const void* b) {
  const RvkPassDraw* drawA = a;
  const RvkPassDraw* drawB = b;
  return compare_i32(&drawA->graphic->renderOrder, &drawB->graphic->renderOrder);
}

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
  buffer->pass  = null;
  buffer->stage = (RvkPassDraw){0};
  dynarray_clear(&buffer->draws);
}

void rend_builder_set_pass(RendBuilderBuffer* buffer, RvkPass* pass) {
  diag_assert_msg(!buffer->pass, "Pass already set");

  buffer->pass = pass;
}

void rend_builder_set_graphic(RendBuilderBuffer* buffer, RvkGraphic* graphic) {
  diag_assert_msg(buffer->pass, "Pass not set");
  diag_assert_msg(!buffer->stage.graphic, "Graphic already set");

  buffer->stage.graphic = graphic;
}

void rend_builder_set_vertex_count(RendBuilderBuffer* buffer, const u32 vertexCount) {
  diag_assert_msg(buffer->pass, "Pass not set");
  diag_assert_msg(!buffer->stage.vertexCountOverride, "Vertex count override already set");

  buffer->stage.vertexCountOverride = vertexCount;
}

void rend_builder_set_draw_mesh(RendBuilderBuffer* buffer, RvkMesh* mesh) {
  diag_assert_msg(buffer->pass, "Pass not set");
  diag_assert_msg(!buffer->stage.drawMesh, "Draw mesh already set");

  buffer->stage.drawMesh = mesh;
}

void rend_builder_set_draw_image(RendBuilderBuffer* buffer, RvkImage* image) {
  diag_assert_msg(buffer->pass, "Pass not set");
  diag_assert_msg(!buffer->stage.drawImage, "Draw image already set");

  rvk_pass_stage_draw_image(buffer->pass, image);
  buffer->stage.drawImage = image;
}

void rend_builder_set_draw_sampler(RendBuilderBuffer* buffer, const RvkSamplerSpec* sampler) {
  diag_assert_msg(buffer->pass, "Pass not set");
  buffer->stage.drawSampler = *sampler;
}

void rend_builder_push(RendBuilderBuffer* buffer) {
  diag_assert_msg(buffer->pass, "Pass not set");
  if (buffer->stage.instCount) {
    *dynarray_push_t(&buffer->draws, RvkPassDraw) = buffer->stage;
  }
  buffer->stage = (RvkPassDraw){0};
}

void rend_builder_discard(RendBuilderBuffer* buffer) {
  diag_assert_msg(buffer->pass, "Pass not set");
  buffer->stage = (RvkPassDraw){0};
}

void rend_builder_flush(RendBuilderBuffer* buffer) {
  rvk_pass_begin(buffer->pass);
  {
    dynarray_sort(&buffer->draws, builder_draw_compare);
    dynarray_for_t(&buffer->draws, RvkPassDraw, draw) { rvk_pass_draw(buffer->pass, draw); }
  }
  rvk_pass_end(buffer->pass);

  rend_builder_clear(buffer);
}
