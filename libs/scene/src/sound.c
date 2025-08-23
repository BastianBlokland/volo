#include "scene/sound.h"

ecs_comp_define(SceneSoundComp);
ecs_comp_define(SceneSoundListenerComp);

ecs_module_init(scene_sound_module) {
  ecs_register_comp(SceneSoundComp);
  ecs_register_comp_empty(SceneSoundListenerComp);
}
