#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, loader_behavior);
  register_spec(check, loader_font_ttf);
  register_spec(check, loader_ftx);
  register_spec(check, loader_graphic);
  register_spec(check, loader_inputmap);
  register_spec(check, loader_mesh_gltf);
  register_spec(check, loader_mesh_obj);
  register_spec(check, loader_raw);
  register_spec(check, loader_shader_spv);
  register_spec(check, loader_texture_atlas);
  register_spec(check, loader_texture_ppm);
  register_spec(check, loader_texture_tga);
  register_spec(check, manager);
}
