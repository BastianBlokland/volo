#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "geo_color.h"
#include "jobs_executor.h"
#include "trace_tracer.h"

#include "builder_internal.h"
#include "rvk/attach_internal.h"
#include "rvk/canvas_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/image_internal.h"
#include "rvk/job_internal.h"
#include "rvk/pass_internal.h"

#define rend_builder_workers_max 8

struct sRendBuilder {
  ALIGNAS(64)
  RvkJobPhase  jobPhaseCurrent;
  RvkCanvas*   canvas;
  RvkPass*     pass;
  RvkPassSetup passSetup;
  RvkPassDraw* draw;
  DynArray     drawList; // RvkPassDraw[]
};

ASSERT(alignof(RendBuilder) == 64, "Unexpected builder alignment")

struct sRendBuilderContainer {
  Allocator*  allocator;
  RendBuilder builders[rend_builder_workers_max];
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
    container->builders[i] = (RendBuilder){
        .drawList = dynarray_create_t(alloc, RvkPassDraw, 8),
    };
  }

  return container;
}

void rend_builder_container_destroy(RendBuilderContainer* container) {
  for (u32 i = 0; i != rend_builder_workers_max; ++i) {
    dynarray_destroy(&container->builders[i].drawList);
  }
  alloc_free_t(container->allocator, container);
}

RendBuilder* rend_builder(const RendBuilderContainer* container) {
  diag_assert(g_jobsWorkerId < rend_builder_workers_max);
  return (RendBuilder*)&container->builders[g_jobsWorkerId];
}

bool rend_builder_canvas_push(
    RendBuilder* b, RvkCanvas* canvas, const RendSettingsComp* settings, const RvkSize windowSize) {
  diag_assert_msg(!b->canvas, "RendBuilder: Canvas already active");

  trace_begin("rend_builder_canvas", TraceColor_Red);

  if (!rvk_canvas_begin(canvas, settings, windowSize)) {
    trace_end();
    return false; // Canvas not ready for rendering.
  }

  b->canvas          = canvas;
  b->jobPhaseCurrent = RvkJobPhase_First;

  return true;
}

void rend_builder_canvas_flush(RendBuilder* b) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");
  diag_assert_msg(!b->pass, "RendBuilder: Pass still active");

  rvk_canvas_end(b->canvas);
  b->canvas = null;

  trace_end();
}

const RvkRepository* rend_builder_repository(RendBuilder* b) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");
  return rvk_canvas_repository(b->canvas);
}

RvkImage* rend_builder_img_swapchain(RendBuilder* b) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");
  return rvk_canvas_swapchain_image(b->canvas);
}

void rend_builder_img_clear_color(RendBuilder* b, RvkImage* img, const GeoColor color) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");
  RvkJob* job = rvk_canvas_job(b->canvas);
  rvk_job_img_clear_color(job, b->jobPhaseCurrent, img, color);
}

void rend_builder_img_clear_depth(RendBuilder* b, RvkImage* img, const f32 depth) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");
  RvkJob* job = rvk_canvas_job(b->canvas);
  rvk_job_img_clear_depth(job, b->jobPhaseCurrent, img, depth);
}

void rend_builder_img_blit(RendBuilder* b, RvkImage* src, RvkImage* dst) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");
  RvkJob* job = rvk_canvas_job(b->canvas);
  rvk_job_img_blit(job, b->jobPhaseCurrent, src, dst);
}

RvkImage* rend_builder_attach_acquire_color(
    RendBuilder* b, RvkPass* pass, const u32 binding, const RvkSize size) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");

  RvkAttachPool*      attachPool = rvk_canvas_attach_pool(b->canvas);
  const RvkAttachSpec spec       = rvk_pass_spec_attach_color(pass, binding);
  return rvk_attach_acquire_color(attachPool, spec, size);
}

RvkImage* rend_builder_attach_acquire_depth(RendBuilder* b, RvkPass* pass, const RvkSize size) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");

  RvkAttachPool*      attachPool = rvk_canvas_attach_pool(b->canvas);
  const RvkAttachSpec spec       = rvk_pass_spec_attach_depth(pass);
  return rvk_attach_acquire_depth(attachPool, spec, size);
}

RvkImage* rend_builder_attach_acquire_copy(RendBuilder* b, RvkImage* src) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");

  RvkImage* res = rend_builder_attach_acquire_copy_uninit(b, src);
  RvkJob*   job = rvk_canvas_job(b->canvas);

  rvk_job_img_copy(job, b->jobPhaseCurrent, src, res);
  return res;
}

RvkImage* rend_builder_attach_acquire_copy_uninit(RendBuilder* b, RvkImage* src) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");

  RvkAttachPool* attachPool = rvk_canvas_attach_pool(b->canvas);

  const RvkAttachSpec spec = {
      .vkFormat     = src->vkFormat,
      .capabilities = src->caps,
  };
  RvkImage* res;
  if (src->type == RvkImageType_DepthAttachment) {
    res = rvk_attach_acquire_depth(attachPool, spec, src->size);
  } else {
    res = rvk_attach_acquire_color(attachPool, spec, src->size);
  }

  return res;
}

void rend_builder_attach_release(RendBuilder* b, RvkImage* img) {
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");

  RvkAttachPool* attachPool = rvk_canvas_attach_pool(b->canvas);
  rvk_attach_release(attachPool, img);
}

void rend_builder_pass_push(RendBuilder* b, RvkPass* pass) {
  diag_assert_msg(!b->pass, "RendBuilder: Pass already active");
  diag_assert_msg(b->canvas, "RendBuilder: Canvas not active");

  MAYBE_UNUSED const String passName = rvk_pass_config(pass)->name;
  trace_begin_msg("rend_builder_pass", TraceColor_White, "pass_{}", fmt_text(passName));

  b->pass      = pass;
  b->passSetup = (RvkPassSetup){0};

  rvk_canvas_pass_push(b->canvas, pass);
}

void rend_builder_pass_flush(RendBuilder* b) {
  diag_assert_msg(b->pass, "RendBuilder: Pass not active");
  diag_assert_msg(!b->draw, "RendBuilder: Draw still active");

  rvk_pass_begin(b->pass, &b->passSetup, b->jobPhaseCurrent);
  dynarray_sort(&b->drawList, builder_draw_compare);
  dynarray_for_t(&b->drawList, RvkPassDraw, draw) { rvk_pass_draw(b->pass, &b->passSetup, draw); }
  rvk_pass_end(b->pass, &b->passSetup);

  dynarray_clear(&b->drawList);

  b->pass = null;

  trace_end();
}

void rend_builder_clear_color(RendBuilder* b, const GeoColor clearColor) {
  diag_assert_msg(b->pass, "RendBuilder: Pass not active");
  b->passSetup.clearColor = clearColor;
}

void rend_builder_attach_color(RendBuilder* b, RvkImage* img, const u16 colorAttachIndex) {
  diag_assert_msg(b->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !b->passSetup.attachColors[colorAttachIndex],
      "RendBuilder: Pass color attachment {} already staged",
      fmt_int(colorAttachIndex));

  b->passSetup.attachColors[colorAttachIndex] = img;
}

void rend_builder_attach_depth(RendBuilder* b, RvkImage* img) {
  diag_assert_msg(b->pass, "RendBuilder: Pass not active");
  diag_assert_msg(!b->passSetup.attachDepth, "RendBuilder: Pass depth attachment already staged");
  b->passSetup.attachDepth = img;
}

Mem rend_builder_global_data(RendBuilder* b, const u32 size, const u16 dataIndex) {
  diag_assert_msg(b->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !b->passSetup.globalData[dataIndex],
      "RendBuilder: Pass global data {} already staged",
      fmt_int(dataIndex));

  RvkJob*                job    = rvk_canvas_job(b->canvas);
  const RvkUniformHandle handle = rvk_job_uniform_push(job, size);

  b->passSetup.globalData[dataIndex] = handle;
  return rvk_job_uniform_map(job, handle);
}

void rend_builder_global_image(RendBuilder* b, RvkImage* img, const u16 imageIndex) {
  diag_assert_msg(b->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !b->passSetup.globalImages[imageIndex],
      "RendBuilder: Pass global image {} already staged",
      fmt_int(imageIndex));

  b->passSetup.globalImages[imageIndex]        = img;
  b->passSetup.globalImageSamplers[imageIndex] = (RvkSamplerSpec){0};
}

void rend_builder_global_image_frozen(RendBuilder* b, const RvkImage* img, const u16 imageIndex) {
  diag_assert_msg(img->frozen, "Image is not frozen");
  // Frozen images are immutable thus we can const-cast them without worry.
  rend_builder_global_image(b, (RvkImage*)img, imageIndex);
}

void rend_builder_global_shadow(RendBuilder* b, RvkImage* img, const u16 imageIndex) {
  diag_assert_msg(b->pass, "RendBuilder: Pass not active");
  diag_assert_msg(
      !b->passSetup.globalImages[imageIndex],
      "RendBuilder: Pass global image {} already staged",
      fmt_int(imageIndex));

  b->passSetup.globalImages[imageIndex]        = img;
  b->passSetup.globalImageSamplers[imageIndex] = (RvkSamplerSpec){
      .flags = RvkSamplerFlags_SupportCompare, // Enable support for sampler2DShadow.
      .wrap  = RvkSamplerWrap_Zero,
  };
}

void rend_builder_draw_push(RendBuilder* b, const RvkGraphic* graphic) {
  diag_assert_msg(b->pass, "RendBuilder: Pass not active");
  diag_assert_msg(!b->draw, "RendBuilder: Draw already active");

  b->draw  = dynarray_push_t(&b->drawList, RvkPassDraw);
  *b->draw = (RvkPassDraw){.graphic = graphic, .drawImageIndex = sentinel_u16};
}

Mem rend_builder_draw_data(RendBuilder* b, const u32 size) {
  diag_assert_msg(b->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!b->draw->drawData, "RendBuilder: Draw-data already set");

  RvkJob*                job    = rvk_canvas_job(b->canvas);
  const RvkUniformHandle handle = rvk_job_uniform_push(job, size);

  b->draw->drawData = handle;
  return rvk_job_uniform_map(job, handle);
}

u32 rend_builder_draw_instances_batch_size(RendBuilder* b, const u32 dataStride) {
  diag_assert_msg(b->draw, "RendBuilder: Draw not active");
  return rvk_pass_batch_size(b->pass, (u32)dataStride);
}

Mem rend_builder_draw_instances(RendBuilder* b, const u32 dataStride, const u32 count) {
  diag_assert_msg(b->draw, "RendBuilder: Draw not active");
  diag_assert_msg(count, "RendBuilder: Needs at least 1 instance");
  diag_assert(count <= rvk_pass_batch_size(b->pass, dataStride));

  RvkJob*   job      = rvk_canvas_job(b->canvas);
  const u32 dataSize = dataStride * count;

  RvkUniformHandle handle = 0;
  if (b->draw->instCount) {
    diag_assert(b->draw->instDataStride == dataStride);
    if (dataStride) {
      handle = rvk_job_uniform_push_next(job, b->draw->instData, dataSize);
    }
  } else {
    b->draw->instDataStride = dataStride;
    if (dataStride) {
      handle            = rvk_job_uniform_push(job, dataSize);
      b->draw->instData = handle;
    }
  }
  b->draw->instCount += count;

  return handle ? rvk_job_uniform_map(job, handle) : mem_empty;
}

void rend_builder_draw_vertex_count(RendBuilder* b, const u32 vertexCount) {
  diag_assert_msg(b->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!b->draw->vertexCountOverride, "RendBuilder: Vertex-count already set");
  b->draw->vertexCountOverride = vertexCount;
}

void rend_builder_draw_mesh(RendBuilder* b, const RvkMesh* mesh) {
  diag_assert_msg(b->draw, "RendBuilder: Draw not active");
  diag_assert_msg(!b->draw->drawMesh, "RendBuilder: Draw-mesh already set");
  b->draw->drawMesh = mesh;
}

void rend_builder_draw_image(RendBuilder* b, RvkImage* img) {
  diag_assert_msg(b->draw, "RendBuilder: Draw not active");
  diag_assert_msg(sentinel_check(b->draw->drawImageIndex), "RendBuilder: Draw-image already set");

  for (u32 i = 0; i != rvk_pass_draw_image_max; ++i) {
    if (b->passSetup.drawImages[i] == img) {
      b->draw->drawImageIndex = (u16)i;
      return; // Image was already staged.
    }
    if (!b->passSetup.drawImages[i]) {
      b->draw->drawImageIndex    = (u16)i;
      b->passSetup.drawImages[i] = img;
      return; // Image is staged in a empty slot.
    }
  }
  diag_assert_fail("Amount of staged per-draw images exceeds the maximum");
}

void rend_builder_draw_image_frozen(RendBuilder* b, const RvkImage* img) {
  diag_assert_msg(img->frozen, "Image is not frozen");
  // Frozen images are immutable thus we can const-cast them without worry.
  rend_builder_draw_image(b, (RvkImage*)img);
}

void rend_builder_draw_sampler(RendBuilder* b, const RvkSamplerSpec samplerSpec) {
  diag_assert_msg(b->draw, "RendBuilder: Draw not active");
  b->draw->drawSampler = samplerSpec;
}

void rend_builder_draw_flush(RendBuilder* b) {
  diag_assert_msg(b->draw, "RendBuilder: Draw not active");
  if (!b->draw->instCount) {
    dynarray_remove(&b->drawList, b->drawList.size - 1, 1);
  }
  b->draw = null;
}
