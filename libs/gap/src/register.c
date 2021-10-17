#include "ecs_def.h"
#include "gap_register.h"

void gap_register(EcsDef* def) {
  ecs_register_module(def, gap_platform_module);
  ecs_register_module(def, gap_window_module);
}
