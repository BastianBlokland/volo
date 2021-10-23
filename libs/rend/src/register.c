#include "ecs_def.h"
#include "rend_register.h"

void rend_register(EcsDef* def) { ecs_register_module(def, rend_canvas_module); }
