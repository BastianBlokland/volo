#include "core_alloc.h"
#include "core_bits.h"
#include "core_math.h"
#include "scene_tag.h"

ecs_comp_define_public(SceneTagComp);

ecs_module_init(scene_tag_module) { ecs_register_comp(SceneTagComp); }
