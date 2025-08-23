#include "ecs/def.h"
#include "vfx/register.h"

void vfx_register(EcsDef* def) {
  ecs_register_module(def, vfx_atlas_module);
  ecs_register_module(def, vfx_decal_module);
  ecs_register_module(def, vfx_rend_module);
  ecs_register_module(def, vfx_stats_module);
  ecs_register_module(def, vfx_system_module);
}
