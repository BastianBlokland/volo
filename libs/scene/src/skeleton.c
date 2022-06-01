#include "core_alloc.h"
#include "scene_skeleton.h"

ecs_comp_define_public(SceneSkeletonTemplateComp);
ecs_comp_define_public(SceneSkeletonComp);

static void ecs_destruct_skeleton_template_comp(void* data) {
  SceneSkeletonComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->jointTransforms, comp->jointCount);
}

static void ecs_destruct_skeleton_comp(void* data) {
  SceneSkeletonComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->jointTransforms, comp->jointCount);
}

ecs_module_init(scene_skeleton_module) {
  ecs_register_comp(SceneSkeletonTemplateComp, .destructor = ecs_destruct_skeleton_template_comp);
  ecs_register_comp(SceneSkeletonComp, .destructor = ecs_destruct_skeleton_comp);
}
