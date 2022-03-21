#include "debug_register.h"
#include "ecs_def.h"

void debug_register(EcsDef* def) {
  ecs_register_module(def, debug_camera_module);
  ecs_register_module(def, debug_grid_module);
  ecs_register_module(def, debug_interface_module);
  ecs_register_module(def, debug_menu_module);
  ecs_register_module(def, debug_rend_module);
}
