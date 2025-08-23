#include "core/diag.h"
#include "ecs/def.h"
#include "ecs/world.h"
#include "rend/register.h"

void rend_register(EcsDef* def) {
  ecs_register_module(def, rend_error_module);
  ecs_register_module(def, rend_fog_module);
  ecs_register_module(def, rend_instance_module);
  ecs_register_module(def, rend_light_module);
  ecs_register_module(def, rend_limiter_module);
  ecs_register_module(def, rend_object_module);
  ecs_register_module(def, rend_painter_module);
  ecs_register_module(def, rend_platform_module);
  ecs_register_module(def, rend_reset_module);
  ecs_register_module(def, rend_resource_module);
  ecs_register_module(def, rend_settings_module);
  ecs_register_module(def, rend_stats_module);
  ecs_register_module(def, rend_terrain_module);
}
