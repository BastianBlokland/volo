#include "ecs/def.h"
#include "scene/register.h"

void scene_register(EcsDef* def, const SceneRegisterContext* ctx) {
  ecs_register_module_ctx(def, scene_action_module, ctx);
  ecs_register_module_ctx(def, scene_attachment_module, ctx);
  ecs_register_module_ctx(def, scene_attack_module, ctx);
  ecs_register_module_ctx(def, scene_bark_module, ctx);
  ecs_register_module_ctx(def, scene_bounds_module, ctx);
  ecs_register_module_ctx(def, scene_camera_module, ctx);
  ecs_register_module_ctx(def, scene_collision_module, ctx);
  ecs_register_module_ctx(def, scene_creator_module, ctx);
  ecs_register_module_ctx(def, scene_faction_module, ctx);
  ecs_register_module_ctx(def, scene_footstep_module, ctx);
  ecs_register_module_ctx(def, scene_health_module, ctx);
  ecs_register_module_ctx(def, scene_level_module, ctx);
  ecs_register_module_ctx(def, scene_lifetime_module, ctx);
  ecs_register_module_ctx(def, scene_light_module, ctx);
  ecs_register_module_ctx(def, scene_location_module, ctx);
  ecs_register_module_ctx(def, scene_locomotion_module, ctx);
  ecs_register_module_ctx(def, scene_marker_module, ctx);
  ecs_register_module_ctx(def, scene_mission_module, ctx);
  ecs_register_module_ctx(def, scene_name_module, ctx);
  ecs_register_module_ctx(def, scene_nav_module, ctx);
  ecs_register_module_ctx(def, scene_prefab_module, ctx);
  ecs_register_module_ctx(def, scene_product_module, ctx);
  ecs_register_module_ctx(def, scene_projectile_module, ctx);
  ecs_register_module_ctx(def, scene_property_module, ctx);
  ecs_register_module_ctx(def, scene_renderable_module, ctx);
  ecs_register_module_ctx(def, scene_script_module, ctx);
  ecs_register_module_ctx(def, scene_set_module, ctx);
  ecs_register_module_ctx(def, scene_skeleton_module, ctx);
  ecs_register_module_ctx(def, scene_sound_module, ctx);
  ecs_register_module_ctx(def, scene_status_module, ctx);
  ecs_register_module_ctx(def, scene_tag_module, ctx);
  ecs_register_module_ctx(def, scene_target_module, ctx);
  ecs_register_module_ctx(def, scene_terrain_module, ctx);
  ecs_register_module_ctx(def, scene_time_module, ctx);
  ecs_register_module_ctx(def, scene_transform_module, ctx);
  ecs_register_module_ctx(def, scene_vfx_module, ctx);
  ecs_register_module_ctx(def, scene_visibility_module, ctx);
  ecs_register_module_ctx(def, scene_weapon_module, ctx);
  if (ctx->devSupport) {
    ecs_register_module_ctx(def, scene_debug_module, ctx);
  }
}
