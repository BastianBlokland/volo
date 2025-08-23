#include "ecs/def.h"
#include "input/register.h"

void input_register(EcsDef* def) {
  ecs_register_module(def, input_manager_module);
  ecs_register_module(def, input_resource_module);
}
