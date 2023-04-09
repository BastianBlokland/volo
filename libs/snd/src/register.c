#include "ecs_def.h"
#include "snd_register.h"

void snd_register(EcsDef* def) {
  ecs_register_module(def, snd_mixer_module);
  ecs_register_module(def, snd_source_module);
}
