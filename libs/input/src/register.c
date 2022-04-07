#include "ecs_def.h"
#include "input_register.h"

void input_register(EcsDef* def) {
  ecs_register_module(def, input_manager_module);
  ecs_register_module(def, input_resource_module);
}
