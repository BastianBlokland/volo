#include "ecs/def.h"
#include "loc/register.h"

void loc_register(EcsDef* def) { ecs_register_module(def, loc_manager_module); }
