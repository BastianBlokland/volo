#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "geo_color.h"
#include "jobs_executor.h"

#include "builder_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/image_internal.h"
#include "rvk/pass_internal.h"

#define rend_builder_workers_max 8

struct sRendBuilderBuffer {
  ALIGNAS(64)
  RvkPass*     pass;
  RvkPassSetup passSetup;
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
  return compare_i32(&drawA->graphic->passOrder, &drawB->graphic->passOrder);
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
  buffer->pass      = pass;
  buffer->passSetup = (RvkPassSetup){0};
}

void rend_builder_pass_flush(RendBuilderBuffer* buffer) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(!buffer->draw, "RendBuilder: Draw still active");

  rvk_pass_begin(buffer->pass, &buffer->passSetup);
  dynarray_sort(&buffer->drawList, builder_draw_compare);
  dynarray_for_t(&buffer->drawList, RvkPassDraw, draw) {
    rvk_pass_draw(buffer->pass, &buffer->passSetup, draw);
  }
  rvk_pass_end(buffer->pass, &buffer->passSetup);

  dynarray_clear(&buffer->drawList);

  buffer->pass = null;
}

void rend_builder_clear_color(RendBuilderBuffer* buffer, const GeoColor clearColor) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");

  buffer->passSetup.clearColor = clearColor;
}

void rend_builder_attach_color(
    RendBuilderBuffer* buffer, RvkImage* img, const u16 colorAttachIndex) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !buffer->passSetup.attachColors[colorAttachIndex],
      "RendBuilder: Pass color attachment {} already staged",
      fmt_int(colorAttachIndex));

  buffer->passSetup.attachColors[colorAttachIndex] = img;
}

void rend_builder_attach_depth(RendBuilderBuffer* buffer, RvkImage* img) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !buffer->passSetup.attachDepth, "RendBuilder: Pass depth attachment already staged");

  buffer->passSetup.attachDepth = img;
}

void rend_builder_global_data(RendBuilderBuffer* buffer, const Mem data, const u16 dataIndex) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !buffer->passSetup.globalData[dataIndex],
      "RendBuilder: Pass global data {} already staged",
      fmt_int(dataIndex));

  buffer->passSetup.globalData[dataIndex] = rvk_pass_uniform_upload(buffer->pass, data);
}

void rend_builder_global_image(RendBuilderBuffer* buffer, RvkImage* img, const u16 imageIndex) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !buffer->passSetup.globalImages[imageIndex],
      "RendBuilder: Pass global image {} already staged",
      fmt_int(imageIndex));

  buffer->passSetup.globalImages[imageIndex]        = img;
  buffer->passSetup.globalImageSamplers[imageIndex] = (RvkSamplerSpec){0};
}

void rend_builder_global_shadow(RendBuilderBuffer* buffer, RvkImage* img, const u16 imageIndex) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !buffer->passSetup.globalImages[imageIndex],
      "RendBuilder: Pass global image {} already staged",
      fmt_int(imageIndex));

  buffer->passSetup.globalImages[imageIndex]        = img;
  buffer->passSetup.globalImageSamplers[imageIndex] = (RvkSamplerSpec){
      .flags = RvkSamplerFlags_SupportCompare, // Enable support for sampler2DShadow.
      .wrap  = RvkSamplerWrap_Zero,
  };
}

void rend_builder_draw_push(RendBuilderBuffer* buffer, const RvkGraphic* graphic) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(!buffer->draw, "RendBuilder: Draw already active");

  buffer->draw  = dynarray_push_t(&buffer->drawList, RvkPassDraw);
  *buffer->draw = (RvkPassDraw){.graphic = graphic, .drawImageIndex = sentinel_u16};
}

void rend_builder_draw_data(RendBuilderBuffer* buffer, const Mem data) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!buffer->draw->drawData, "RendBuilder: Draw-data already set");

  buffer->draw->drawData = rvk_pass_uniform_upload(buffer->pass, data);
}

u32 rend_builder_draw_instances_batch_size(RendBuilderBuffer* buffer, const u32 dataStride) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  return rvk_pass_batch_size(buffer->pass, dataStride);
}

void rend_builder_draw_instances(RendBuilderBuffer* buffer, const Mem data, const u32 count) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(count, "RendBuilder: Needs at least 1 instance");

  const u32 dataStride = (u32)(data.size / count);
  diag_assert(count <= rvk_pass_batch_size(buffer->pass, dataStride));

  if (buffer->draw->instCount) {
    diag_assert(buffer->draw->instDataStride == dataStride);
    if (dataStride) {
      rvk_pass_uniform_upload_next(buffer->pass, buffer->draw->instData, data);
    }
  } else {
    buffer->draw->instDataStride = dataStride;
    if (dataStride) {
      buffer->draw->instData = rvk_pass_uniform_upload(buffer->pass, data);
    }
  }
  buffer->draw->instCount += count;
}

void rend_builder_draw_vertex_count(RendBuilderBuffer* buffer, const u32 vertexCount) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!buffer->draw->vertexCountOverride, "RendBuilder: Vertex-count already set");

  buffer->draw->vertexCountOverride = vertexCount;
}

void rend_builder_draw_mesh(RendBuilderBuffer* buffer, const RvkMesh* mesh) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!buffer->draw->drawMesh, "RendBuilder: Draw-mesh already set");

  buffer->draw->drawMesh = mesh;
}

void rend_builder_draw_image(RendBuilderBuffer* buffer, RvkImage* image) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(
      sentinel_check(buffer->draw->drawImageIndex), "RendBuilder: Draw-image already set");

  for (u32 i = 0; i != rvk_pass_draw_image_max; ++i) {
    if (buffer->passSetup.drawImages[i] == image) {
      buffer->draw->drawImageIndex = (u16)i;
      return; // Image was already staged.
    }
    if (!buffer->passSetup.drawImages[i]) {
      buffer->draw->drawImageIndex    = (u16)i;
      buffer->passSetup.drawImages[i] = image;
      return; // Image is staged in a empty slot.
    }
  }
  diag_assert_fail("Amount of staged per-draw images exceeds the maximum");
}

void rend_builder_draw_image_frozen(RendBuilderBuffer* buffer, const RvkImage* image) {
  diag_assert_msg(image->frozen, "Image is not frozen");
  // Frozen images are immutable thus we can const-cast them without worry.
  rend_builder_draw_image(buffer, (RvkImage*)image);
}

void rend_builder_draw_sampler(RendBuilderBuffer* buffer, const RvkSamplerSpec samplerSpec) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  buffer->draw->drawSampler = samplerSpec;
}

void rend_builder_draw_flush(RendBuilderBuffer* buffer) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  if (!buffer->draw->instCount) {
    dynarray_remove(&buffer->drawList, buffer->drawList.size - 1, 1);
  }
  buffer->draw = null;
}
