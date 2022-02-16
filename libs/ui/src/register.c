#include "ecs_def.h"
#include "ui_register.h"

void ui_register(EcsDef* def) { ecs_register_module(def, ui_canvas_module); }
