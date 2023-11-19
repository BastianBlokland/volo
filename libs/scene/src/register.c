#include "ecs_def.h"
#include "scene_register.h"

void scene_register(EcsDef* def) {
  ecs_register_module(def, scene_attachment_module);
  ecs_register_module(def, scene_attack_module);
  ecs_register_module(def, scene_bounds_module);
  ecs_register_module(def, scene_camera_module);
  ecs_register_module(def, scene_collision_module);
  ecs_register_module(def, scene_faction_module);
  ecs_register_module(def, scene_footstep_module);
  ecs_register_module(def, scene_health_module);
  ecs_register_module(def, scene_knowledge_module);
  ecs_register_module(def, scene_level_module);
  ecs_register_module(def, scene_lifetime_module);
  ecs_register_module(def, scene_location_module);
  ecs_register_module(def, scene_locomotion_module);
  ecs_register_module(def, scene_name_module);
  ecs_register_module(def, scene_nav_module);
  ecs_register_module(def, scene_prefab_module);
  ecs_register_module(def, scene_product_module);
  ecs_register_module(def, scene_projectile_module);
  ecs_register_module(def, scene_renderable_module);
  ecs_register_module(def, scene_script_module);
  ecs_register_module(def, scene_set_module);
  ecs_register_module(def, scene_skeleton_module);
  ecs_register_module(def, scene_sound_module);
  ecs_register_module(def, scene_status_module);
  ecs_register_module(def, scene_tag_module);
  ecs_register_module(def, scene_target_module);
  ecs_register_module(def, scene_taunt_module);
  ecs_register_module(def, scene_terrain_module);
  ecs_register_module(def, scene_time_module);
  ecs_register_module(def, scene_transform_module);
  ecs_register_module(def, scene_vfx_module);
  ecs_register_module(def, scene_visibility_module);
  ecs_register_module(def, scene_weapon_module);
}
