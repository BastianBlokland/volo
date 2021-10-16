#include "ecs_def.h"
#include "gapp_register.h"

void gapp_register(EcsDef* def) {
  ecs_register_module(def, gapp_app_module);
  ecs_register_module(def, gapp_window_module);
}
