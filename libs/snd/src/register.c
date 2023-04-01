#include "ecs_def.h"
#include "snd_register.h"

void snd_register(EcsDef* def) { ecs_register_module(def, snd_output_module); }
