
#include "asset_register.h"
#include "ecs_def.h"

void asset_register(EcsDef* def) {
  ecs_register_module(def, asset_manager_module);
  ecs_register_module(def, asset_raw_module);
}
