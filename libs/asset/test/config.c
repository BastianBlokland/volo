#include "app_check.h"

void app_check_init(CheckDef* check) {
  register_spec(check, loader_font_ttf);
  register_spec(check, loader_graphic);
  register_spec(check, loader_inputmap);
  register_spec(check, loader_level);
  register_spec(check, loader_mesh_gltf);
  register_spec(check, loader_mesh_obj);
  register_spec(check, loader_prefab);
  register_spec(check, loader_product);
  register_spec(check, loader_raw);
  register_spec(check, loader_script);
  register_spec(check, loader_shader_spv);
  register_spec(check, loader_terrain);
  register_spec(check, loader_texture_atlas);
  register_spec(check, loader_texture_font);
  register_spec(check, loader_texture_png);
  register_spec(check, loader_texture_ppm);
  register_spec(check, loader_texture_tga);
  register_spec(check, loader_weapon);
  register_spec(check, manager);
}

void app_check_teardown(void) {}
