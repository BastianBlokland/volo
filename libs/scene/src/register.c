#include "ecs_def.h"
#include "scene_register.h"

void scene_register(EcsDef* def) {
  ecs_register_module(def, scene_camera_module);
  ecs_register_module(def, scene_graphic_module);
  ecs_register_module(def, scene_transform_module);
}
