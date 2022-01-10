#include "core_alloc.h"
#include "core_math.h"
#include "scene_renderable.h"

ecs_comp_define_public(SceneRenderableComp);
ecs_comp_define_public(SceneRenderableUniqueComp);

static void ecs_destruct_renderable_unique(void* data) {
  SceneRenderableUniqueComp* comp = data;
  if (comp->instDataMem.ptr) {
    alloc_free(g_alloc_heap, comp->instDataMem);
  }
}

ecs_module_init(scene_renderable_module) {
  ecs_register_comp(SceneRenderableComp);
  ecs_register_comp(SceneRenderableUniqueComp, .destructor = ecs_destruct_renderable_unique);
}

Mem scene_renderable_unique_data(const SceneRenderableUniqueComp* renderable) {
  return mem_slice(renderable->instDataMem, 0, renderable->instDataSize);
}

Mem scene_renderable_unique_data_alloc(SceneRenderableUniqueComp* renderable, const usize size) {
  if (renderable->instDataMem.size >= size) {
    renderable->instDataSize = size;
    return mem_slice(renderable->instDataMem, 0, size);
  }

  if (LIKELY(renderable->instDataMem.ptr)) {
    alloc_free(g_alloc_heap, renderable->instDataMem);
  }
  renderable->instDataMem  = alloc_alloc(g_alloc_heap, size, math_min(size, 16));
  renderable->instDataSize = size;
  return renderable->instDataMem;
}
