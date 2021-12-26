#include "rend_instance.h"

ecs_comp_define_public(RendInstanceComp);

ecs_module_init(rend_instance_module) { ecs_register_comp(RendInstanceComp); }
