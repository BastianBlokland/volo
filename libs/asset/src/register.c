
#include "asset/data.h"
#include "asset/register.h"
#include "ecs/def.h"

void asset_register(EcsDef* def, const AssetRegisterContext* ctx) {
  asset_data_init(ctx->devSupport);

  ecs_register_module_ctx(def, asset_decal_module, ctx);
  ecs_register_module_ctx(def, asset_font_module, ctx);
  ecs_register_module_ctx(def, asset_graphic_module, ctx);
  ecs_register_module_ctx(def, asset_icon_module, ctx);
  ecs_register_module_ctx(def, asset_import_module, ctx);
  ecs_register_module_ctx(def, asset_inputmap_module, ctx);
  ecs_register_module_ctx(def, asset_level_module, ctx);
  ecs_register_module_ctx(def, asset_locale_module, ctx);
  ecs_register_module_ctx(def, asset_manager_module, ctx);
  ecs_register_module_ctx(def, asset_mesh_gltf_module, ctx);
  ecs_register_module_ctx(def, asset_mesh_module, ctx);
  ecs_register_module_ctx(def, asset_prefab_module, ctx);
  ecs_register_module_ctx(def, asset_product_module, ctx);
  ecs_register_module_ctx(def, asset_raw_module, ctx);
  ecs_register_module_ctx(def, asset_script_module, ctx);
  ecs_register_module_ctx(def, asset_shader_glsl_module, ctx);
  ecs_register_module_ctx(def, asset_shader_module, ctx);
  ecs_register_module_ctx(def, asset_sound_module, ctx);
  ecs_register_module_ctx(def, asset_terrain_module, ctx);
  ecs_register_module_ctx(def, asset_texture_array_module, ctx);
  ecs_register_module_ctx(def, asset_texture_atlas_module, ctx);
  ecs_register_module_ctx(def, asset_texture_font_module, ctx);
  ecs_register_module_ctx(def, asset_texture_module, ctx);
  ecs_register_module_ctx(def, asset_vfx_module, ctx);
  ecs_register_module_ctx(def, asset_weapon_module, ctx);
}
