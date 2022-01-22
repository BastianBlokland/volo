#include "ecs_def.h"
#include "scene_register.h"

void scene_register(EcsDef* def) {
  ecs_register_module(def, scene_camera_module);
  ecs_register_module(def, scene_grid_module);
  ecs_register_module(def, scene_renderable_module);
  ecs_register_module(def, scene_text_module);
  ecs_register_module(def, scene_time_module);
  ecs_register_module(def, scene_transform_module);
  ecs_register_module(def, scene_velocity_module);
}
