#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "jobs_executor.h"

#include "builder_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/pass_internal.h"

#define rend_builder_workers_max 8
#define rend_builder_draw_data_align 16
#define rend_builder_draw_data_chunk_size (32 * usize_kibibyte)

struct sRendBuilderBuffer {
  ALIGNAS(64)
  RvkPass*     pass;
  RvkPassDraw* draw;
  DynArray     drawList; // RvkPassDraw[]
  Allocator*   drawDataAlloc;
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
        .drawDataAlloc =
            alloc_chunked_create(alloc, alloc_bump_create, rend_builder_draw_data_chunk_size),
    };
  }

  return builder;
}

void rend_builder_destroy(RendBuilder* builder) {
  for (u32 i = 0; i != rend_builder_workers_max; ++i) {
    dynarray_destroy(&builder->buffers[i].drawList);
    alloc_chunked_destroy(builder->buffers[i].drawDataAlloc);
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

  alloc_reset(buffer->drawDataAlloc);

  buffer->pass = null;
}

void rend_builder_draw_push(RendBuilderBuffer* buffer, RvkGraphic* graphic) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(!buffer->draw, "RendBuilder: Draw already active");

  buffer->draw  = dynarray_push_t(&buffer->drawList, RvkPassDraw);
  *buffer->draw = (RvkPassDraw){.graphic = graphic};
}

Mem rend_builder_draw_data(RendBuilderBuffer* buffer, const usize size) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!mem_valid(buffer->draw->drawData), "RendBuilder: Draw-data already set");

  const Mem result = alloc_alloc(buffer->drawDataAlloc, size, rend_builder_draw_data_align);
  diag_assert_msg(mem_valid(result), "RendBuilder: Draw-data allocator ran out of space");
  buffer->draw->drawData = result;
  return result;
}

void rend_builder_draw_data_extern(RendBuilderBuffer* buffer, const Mem drawData) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!mem_valid(buffer->draw->drawData), "RendBuilder: Draw-data already set");

  buffer->draw->drawData = drawData;
}

Mem rend_builder_draw_instances(RendBuilderBuffer* buffer, const u32 count, const u32 stride) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!buffer->draw->instCount, "RendBuilder: Instances already set");
  diag_assert_msg(!mem_valid(buffer->draw->instData), "RendBuilder: Instance data already set");

  const Mem data = alloc_alloc(buffer->drawDataAlloc, count * stride, rend_builder_draw_data_align);
  diag_assert_msg(mem_valid(data), "RendBuilder: Draw-data allocator ran out of space");

  buffer->draw->instCount      = count;
  buffer->draw->instDataStride = stride;
  buffer->draw->instData       = data;
  return data;
}

void rend_builder_draw_instances_trim(RendBuilderBuffer* buffer, const u32 count) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(buffer->draw->instCount, "RendBuilder: No instances set");

  diag_assert(count <= buffer->draw->instCount);

  buffer->draw->instCount = count;
}

void rend_builder_draw_instances_extern(
    RendBuilderBuffer* buffer, const u32 count, const Mem data, const u32 stride) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!buffer->draw->instCount, "RendBuilder: Instances already set");
  diag_assert_msg(!mem_valid(buffer->draw->instData), "RendBuilder: Instance data already set");

  diag_assert(data.size == count * stride);

  buffer->draw->instCount      = count;
  buffer->draw->instDataStride = stride;
  buffer->draw->instData       = data;
}

void rend_builder_draw_vertex_count(RendBuilderBuffer* buffer, const u32 vertexCount) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!buffer->draw->vertexCountOverride, "RendBuilder: Vertex-count already set");

  buffer->draw->vertexCountOverride = vertexCount;
}

void rend_builder_draw_mesh(RendBuilderBuffer* buffer, RvkMesh* mesh) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!buffer->draw->drawMesh, "RendBuilder: Draw-mesh already set");

  buffer->draw->drawMesh = mesh;
}

void rend_builder_draw_image(RendBuilderBuffer* buffer, RvkImage* image) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!buffer->draw->drawImage, "RendBuilder: Draw-image already set");

  rvk_pass_stage_draw_image(buffer->pass, image);
  buffer->draw->drawImage = image;
}

void rend_builder_draw_sampler(RendBuilderBuffer* buffer, const RvkSamplerSpec samplerSpec) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
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
