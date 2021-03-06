#include "ecs_def.h"
#include "scene_register.h"

void scene_register(EcsDef* def) {
  ecs_register_module(def, scene_bounds_module);
  ecs_register_module(def, scene_camera_module);
  ecs_register_module(def, scene_collision_module);
  ecs_register_module(def, scene_lifetime_module);
  ecs_register_module(def, scene_locomotion_module);
  ecs_register_module(def, scene_name_module);
  ecs_register_module(def, scene_renderable_module);
  ecs_register_module(def, scene_selection_module);
  ecs_register_module(def, scene_skeleton_module);
  ecs_register_module(def, scene_tag_module);
  ecs_register_module(def, scene_time_module);
  ecs_register_module(def, scene_transform_module);
}
