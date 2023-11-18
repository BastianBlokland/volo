#include "scene_set.h"

ecs_comp_define(SceneSetEnvComp) { u32 dummy; };

ecs_comp_define_public(SceneSetMemberComp);

static void ecs_destruct_set_env_comp(void* data) {
  SceneSetEnvComp* env = data;
  (void)env;
}

ecs_module_init(scene_set_module) {
  ecs_register_comp(SceneSetEnvComp, .destructor = ecs_destruct_set_env_comp);
  ecs_register_comp(SceneSetMemberComp);
}
