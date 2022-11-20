#include "ecs_def.h"
#include "scene_register.h"

void scene_register(EcsDef* def) {
  ecs_register_module(def, scene_attachment_module);
  ecs_register_module(def, scene_attack_module);
  ecs_register_module(def, scene_bounds_module);
  ecs_register_module(def, scene_brain_module);
  ecs_register_module(def, scene_camera_module);
  ecs_register_module(def, scene_collision_module);
  ecs_register_module(def, scene_controller_module);
  ecs_register_module(def, scene_faction_module);
  ecs_register_module(def, scene_health_module);
  ecs_register_module(def, scene_lifetime_module);
  ecs_register_module(def, scene_locomotion_module);
  ecs_register_module(def, scene_name_module);
  ecs_register_module(def, scene_nav_module);
  ecs_register_module(def, scene_prefab_module);
  ecs_register_module(def, scene_projectile_module);
  ecs_register_module(def, scene_renderable_module);
  ecs_register_module(def, scene_selection_module);
  ecs_register_module(def, scene_sensor_module);
  ecs_register_module(def, scene_skeleton_module);
  ecs_register_module(def, scene_spawner_module);
  ecs_register_module(def, scene_tag_module);
  ecs_register_module(def, scene_target_module);
  ecs_register_module(def, scene_time_module);
  ecs_register_module(def, scene_transform_module);
  ecs_register_module(def, scene_unit_module);
  ecs_register_module(def, scene_vfx_module);
  ecs_register_module(def, scene_weapon_module);
}
