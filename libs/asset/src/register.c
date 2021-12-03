
#include "asset_register.h"
#include "ecs_def.h"

void asset_register(EcsDef* def) {
  ecs_register_module(def, asset_manager_module);
  ecs_register_module(def, asset_material_module);
  ecs_register_module(def, asset_mesh_module);
  ecs_register_module(def, asset_raw_module);
  ecs_register_module(def, asset_shader_module);
  ecs_register_module(def, asset_texture_module);
}
