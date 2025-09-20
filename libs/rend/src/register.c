#include "ecs/def.h"
#include "rend/register.h"

void rend_register(EcsDef* def, const RendRegisterContext* ctx) {
  ecs_register_module_ctx(def, rend_error_module, ctx);
  ecs_register_module_ctx(def, rend_fog_module, ctx);
  ecs_register_module_ctx(def, rend_instance_module, ctx);
  ecs_register_module_ctx(def, rend_light_module, ctx);
  ecs_register_module_ctx(def, rend_limiter_module, ctx);
  ecs_register_module_ctx(def, rend_object_module, ctx);
  ecs_register_module_ctx(def, rend_painter_module, ctx);
  ecs_register_module_ctx(def, rend_platform_module, ctx);
  ecs_register_module_ctx(def, rend_reset_module, ctx);
  ecs_register_module_ctx(def, rend_resource_module, ctx);
  ecs_register_module_ctx(def, rend_settings_module, ctx);
  ecs_register_module_ctx(def, rend_terrain_module, ctx);
  if (ctx->enableStats) {
    ecs_register_module(def, rend_stats_module);
  }
}
