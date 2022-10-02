#include "ecs_def.h"
#include "vfx_register.h"

void vfx_register(EcsDef* def) {
  ecs_register_module(def, vfx_emitter_module);
  ecs_register_module(def, vfx_particle_module);
}
