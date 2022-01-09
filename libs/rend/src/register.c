#include "core_diag.h"
#include "ecs_def.h"
#include "ecs_world.h"
#include "rend_register.h"

void rend_register(EcsDef* def) {
  ecs_register_module(def, rend_platform_module);
  ecs_register_module(def, rend_resource_module);
  ecs_register_module(def, rend_instance_module);
  ecs_register_module(def, rend_stats_module);
  ecs_register_module(def, rend_painter_module);
}
