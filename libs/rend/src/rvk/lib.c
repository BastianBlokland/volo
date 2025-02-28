#include "log_logger.h"

#include "lib_internal.h"
#include "mem_internal.h"

RvkLib* rvk_lib_create(const RendSettingsGlobalComp* set) {
  (void)set;

  RvkLib* lib = alloc_alloc_t(g_allocHeap, RvkLib);

  *lib = (RvkLib){
      .vkAlloc = rvk_mem_allocator(g_allocHeap),
  };

  log_i("Vulkan library created");

  return lib;
}

void rvk_lib_destroy(RvkLib* lib) {

  vkDestroyInstance(lib->vkInst, &lib->vkAlloc);
  alloc_free_t(g_allocHeap, lib);

  log_d("Vulkan library destroyed");
}
