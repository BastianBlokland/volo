
#include "asset_register.h"
#include "ecs_def.h"

#include "data_internal.h"

void asset_register(EcsDef* def) {
  asset_data_init();

  ecs_register_module(def, asset_cursor_module);
  ecs_register_module(def, asset_decal_module);
  ecs_register_module(def, asset_font_module);
  ecs_register_module(def, asset_graphic_module);
  ecs_register_module(def, asset_inputmap_module);
  ecs_register_module(def, asset_level_module);
  ecs_register_module(def, asset_manager_module);
  ecs_register_module(def, asset_mesh_gltf_module);
  ecs_register_module(def, asset_mesh_module);
  ecs_register_module(def, asset_prefab_module);
  ecs_register_module(def, asset_product_module);
  ecs_register_module(def, asset_raw_module);
  ecs_register_module(def, asset_script_module);
  ecs_register_module(def, asset_shader_glsl_module);
  ecs_register_module(def, asset_shader_module);
  ecs_register_module(def, asset_sound_module);
  ecs_register_module(def, asset_terrain_module);
  ecs_register_module(def, asset_texture_array_module);
  ecs_register_module(def, asset_texture_atlas_module);
  ecs_register_module(def, asset_texture_font_module);
  ecs_register_module(def, asset_texture_module);
  ecs_register_module(def, asset_vfx_module);
  ecs_register_module(def, asset_weapon_module);
}
