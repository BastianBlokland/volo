#include "ecs_def.h"
#include "ui_register.h"

void ui_register(EcsDef* def) {
  ecs_register_module(def, ui_canvas_module);
  ecs_register_module(def, ui_resource_module);
  ecs_register_module(def, ui_settings_module);
}
