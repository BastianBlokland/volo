#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "geo_color.h"
#include "jobs_executor.h"

#include "builder_internal.h"
#include "rvk/canvas_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/image_internal.h"
#include "rvk/pass_internal.h"

#define rend_builder_workers_max 8

struct sRendBuilderBuffer {
  ALIGNAS(64)
  RvkCanvas*   canvas;
  RvkPass*     pass;
  RvkPassSetup passSetup;
  RvkPassDraw* draw;
  DynArray     drawList; // RvkPassDraw[]
};

ASSERT(alignof(RendBuilderBuffer) == 64, "Unexpected buffer alignment")

struct sRendBuilderContainer {
  Allocator*        allocator;
  RendBuilderBuffer buffers[rend_builder_workers_max];
};

static i8 builder_draw_compare(const void* a, const void* b) {
  const RvkPassDraw* drawA = a;
  const RvkPassDraw* drawB = b;
  return compare_i32(&drawA->graphic->passOrder, &drawB->graphic->passOrder);
}

RendBuilderContainer* rend_builder_container_create(Allocator* alloc) {
  RendBuilderContainer* container = alloc_alloc_t(alloc, RendBuilderContainer);

  *container = (RendBuilderContainer){.allocator = alloc};

  for (u32 i = 0; i != rend_builder_workers_max; ++i) {
    container->buffers[i] = (RendBuilderBuffer){
        .drawList = dynarray_create_t(alloc, RvkPassDraw, 8),
    };
  }

  return container;
}

void rend_builder_container_destroy(RendBuilderContainer* container) {
  for (u32 i = 0; i != rend_builder_workers_max; ++i) {
    dynarray_destroy(&container->buffers[i].drawList);
  }
  alloc_free_t(container->allocator, container);
}

RendBuilderBuffer* rend_builder_buffer(const RendBuilderContainer* container) {
  diag_assert(g_jobsWorkerId < rend_builder_workers_max);
  return (RendBuilderBuffer*)&container->buffers[g_jobsWorkerId];
}

bool rend_builder_canvas_push(
    RendBuilderBuffer*      buffer,
    RvkCanvas*              canvas,
    const RendSettingsComp* settings,
    const RvkSize           windowSize) {
  diag_assert_msg(!buffer->canvas, "RendBuilder: Canvas already active");

  if (!rvk_canvas_begin(canvas, settings, windowSize)) {
    return false; // Canvas not ready for rendering.
  }
  buffer->canvas = canvas;
  return true;
}

void rend_builder_canvas_flush(RendBuilderBuffer* buffer) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  diag_assert_msg(!buffer->pass, "RendBuilder: Pass still active");

  rvk_canvas_end(buffer->canvas);
  buffer->canvas = null;
}

const RvkRepository* rend_builder_repository(RendBuilderBuffer* buffer) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  return rvk_canvas_repository(buffer->canvas);
}

RvkImage* rend_builder_img_swapchain(RendBuilderBuffer* buffer) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  return rvk_canvas_swapchain_image(buffer->canvas);
}

void rend_builder_img_clear_color(RendBuilderBuffer* buffer, RvkImage* img, const GeoColor color) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  rvk_canvas_img_clear_color(buffer->canvas, img, color);
}

void rend_builder_img_clear_depth(RendBuilderBuffer* buffer, RvkImage* img, const f32 depth) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  rvk_canvas_img_clear_depth(buffer->canvas, img, depth);
}

void rend_builder_img_blit(RendBuilderBuffer* buffer, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  rvk_canvas_img_blit(buffer->canvas, src, dst);
}

RvkImage* rend_builder_attach_acquire_color(
    RendBuilderBuffer* buffer, RvkPass* pass, const u32 binding, const RvkSize size) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  return rvk_canvas_attach_acquire_color(buffer->canvas, pass, binding, size);
}

RvkImage*
rend_builder_attach_acquire_depth(RendBuilderBuffer* buffer, RvkPass* pass, const RvkSize size) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  return rvk_canvas_attach_acquire_depth(buffer->canvas, pass, size);
}

RvkImage* rend_builder_attach_acquire_copy(RendBuilderBuffer* buffer, RvkImage* img) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  return rvk_canvas_attach_acquire_copy(buffer->canvas, img);
}

RvkImage* rend_builder_attach_acquire_copy_uninit(RendBuilderBuffer* buffer, RvkImage* img) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  return rvk_canvas_attach_acquire_copy_uninit(buffer->canvas, img);
}

void rend_builder_attach_release(RendBuilderBuffer* buffer, RvkImage* img) {
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");
  rvk_canvas_attach_release(buffer->canvas, img);
}

void rend_builder_pass_push(RendBuilderBuffer* buffer, RvkPass* pass) {
  diag_assert_msg(!buffer->pass, "RendBuilder: Pass already active");
  diag_assert_msg(buffer->canvas, "RendBuilder: Canvas not active");

  buffer->pass      = pass;
  buffer->passSetup = (RvkPassSetup){0};

  rvk_canvas_pass_push(buffer->canvas, pass);
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

Mem rend_builder_global_data(RendBuilderBuffer* buffer, const u32 size, const u16 dataIndex) {
  diag_assert_msg(buffer->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !buffer->passSetup.globalData[dataIndex],
      "RendBuilder: Pass global data {} already staged",
      fmt_int(dataIndex));

  const RvkUniformHandle handle           = rvk_pass_uniform_push(buffer->pass, size);
  buffer->passSetup.globalData[dataIndex] = handle;
  return rvk_pass_uniform_map(buffer->pass, handle);
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

void rend_builder_global_image_frozen(
    RendBuilderBuffer* buffer, const RvkImage* img, const u16 imageIndex) {
  diag_assert_msg(img->frozen, "Image is not frozen");
  // Frozen images are immutable thus we can const-cast them without worry.
  rend_builder_global_image(buffer, (RvkImage*)img, imageIndex);
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

Mem rend_builder_draw_data(RendBuilderBuffer* buffer, const u32 size) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!buffer->draw->drawData, "RendBuilder: Draw-data already set");

  const RvkUniformHandle handle = rvk_pass_uniform_push(buffer->pass, size);
  buffer->draw->drawData        = handle;
  return rvk_pass_uniform_map(buffer->pass, handle);
}

u32 rend_builder_draw_instances_batch_size(RendBuilderBuffer* buffer, const u32 dataStride) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  return rvk_pass_batch_size(buffer->pass, (u32)dataStride);
}

Mem rend_builder_draw_instances(RendBuilderBuffer* buffer, const u32 dataStride, const u32 count) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(count, "RendBuilder: Needs at least 1 instance");
  diag_assert(count <= rvk_pass_batch_size(buffer->pass, dataStride));

  const u32 dataSize = dataStride * count;

  RvkUniformHandle handle = 0;
  if (buffer->draw->instCount) {
    diag_assert(buffer->draw->instDataStride == dataStride);
    if (dataStride) {
      handle = rvk_pass_uniform_push_next(buffer->pass, buffer->draw->instData, dataSize);
    }
  } else {
    buffer->draw->instDataStride = dataStride;
    if (dataStride) {
      handle                 = rvk_pass_uniform_push(buffer->pass, dataSize);
      buffer->draw->instData = handle;
    }
  }
  buffer->draw->instCount += count;

  return handle ? rvk_pass_uniform_map(buffer->pass, handle) : mem_empty;
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

void rend_builder_draw_image(RendBuilderBuffer* buffer, RvkImage* img) {
  diag_assert_msg(buffer->draw, "RendBuilder: Draw not active");
  diag_assert_msg(
      sentinel_check(buffer->draw->drawImageIndex), "RendBuilder: Draw-image already set");

  for (u32 i = 0; i != rvk_pass_draw_image_max; ++i) {
    if (buffer->passSetup.drawImages[i] == img) {
      buffer->draw->drawImageIndex = (u16)i;
      return; // Image was already staged.
    }
    if (!buffer->passSetup.drawImages[i]) {
      buffer->draw->drawImageIndex    = (u16)i;
      buffer->passSetup.drawImages[i] = img;
      return; // Image is staged in a empty slot.
    }
  }
  diag_assert_fail("Amount of staged per-draw images exceeds the maximum");
}

void rend_builder_draw_image_frozen(RendBuilderBuffer* buffer, const RvkImage* img) {
  diag_assert_msg(img->frozen, "Image is not frozen");
  // Frozen images are immutable thus we can const-cast them without worry.
  rend_builder_draw_image(buffer, (RvkImage*)img);
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
