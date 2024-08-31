#include "ecs_def.h"
#include "vfx_register.h"

void vfx_register(EcsDef* def) {
  ecs_register_module(def, vfx_atlas_module);
  ecs_register_module(def, vfx_decal_module);
  ecs_register_module(def, vfx_rend_module);
  ecs_register_module(def, vfx_stats_module);
  ecs_register_module(def, vfx_system_module);
}
