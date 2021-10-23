#include "log_logger.h"

#include "context_internal.h"

struct sRendContextVk {
  Allocator* alloc;
};

RendContextVk* rend_vk_context_create(Allocator* alloc) {
  RendContextVk* ctx = alloc_alloc_t(alloc, RendContextVk);
  *ctx               = (RendContextVk){.alloc = alloc};

  log_i("Vulkan context created");

  return ctx;
}

void rend_vk_context_destroy(RendContextVk* ctx) {

  log_i("Vulkan context destroyed");

  alloc_free_t(ctx->alloc, ctx);
}
