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
  RvkPass*     pass;
  RvkPassDraw* draw;
  DynArray     drawList; // RvkPassDraw[]
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
    builder->buffers[i] = (RendBuilderBuffer){
        .drawList = dynarray_create_t(alloc, RvkPassDraw, 8),
    };
  }

  return builder;
}

void rend_builder_destroy(RendBuilder* builder) {
  for (u32 i = 0; i != rend_builder_workers_max; ++i) {
    dynarray_destroy(&builder->buffers[i].drawList);
  }
  alloc_free_t(builder->allocator, builder);
}

RendBuilderBuffer* rend_builder_buffer(const RendBuilder* builder) {
  diag_assert(g_jobsWorkerId < rend_builder_workers_max);
  return (RendBuilderBuffer*)&builder->buffers[g_jobsWorkerId];
}

void rend_builder_pass_push(RendBuilderBuffer* buffer, RvkPass* pass) {
  diag_assert_msg(!buffer->pass, "RendBuilder: Pass already active");
  buffer->pass = pass;
}

void rend_builder_pass_flush(RendBuilderBuffer* buffer) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(!buffer->draw, "RendBuilder: Draw still active");

  if (buffer->drawList.size) {
    rvk_pass_begin(buffer->pass);
    {
      dynarray_sort(&buffer->drawList, builder_draw_compare);
      dynarray_for_t(&buffer->drawList, RvkPassDraw, draw) { rvk_pass_draw(buffer->pass, draw); }
    }
    rvk_pass_end(buffer->pass);
    dynarray_clear(&buffer->drawList);
  }

  buffer->pass = null;
}

void rend_builder_draw_push(RendBuilderBuffer* buffer, RvkGraphic* graphic) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(!buffer->draw, "RendBuilder: Draw already active");

  buffer->draw  = dynarray_push_t(&buffer->drawList, RvkPassDraw);
  *buffer->draw = (RvkPassDraw){.graphic = graphic};
}

void rend_builder_draw_vertex_count(RendBuilderBuffer* buffer, const u32 vertexCount) {
  buffer->draw->vertexCountOverride = vertexCount;
}

void rend_builder_draw_mesh(RendBuilderBuffer* buffer, RvkMesh* mesh) {
  buffer->draw->drawMesh = mesh;
}

void rend_builder_draw_image(RendBuilderBuffer* buffer, RvkImage* image) {
  rvk_pass_stage_draw_image(buffer->pass, image);
  buffer->draw->drawImage = image;
}

void rend_builder_draw_sampler(RendBuilderBuffer* buffer, const RvkSamplerSpec samplerSpec) {
  buffer->draw->drawSampler = samplerSpec;
}

void rend_builder_draw_flush(RendBuilderBuffer* buffer) {
  diag_assert_msg(!buffer->draw, "RendBuilder: Draw not active");
  buffer->draw = null;
}

void rend_builder_draw_discard(RendBuilderBuffer* buffer) {
  diag_assert_msg(!buffer->draw, "RendBuilder: Draw not active");
  dynarray_remove(&buffer->drawList, buffer->drawList.size - 1, 1);
  buffer->draw = null;
}
