#include "core_alloc.h"
#include "ecs_def.h"
#include "snd_register.h"

// Temp!
#include "device_internal.h"

void snd_register(EcsDef* def) {
  (void)def;

  // Temp!
  SndDevice* dev = snd_device_create(g_alloc_heap);
  snd_device_destroy(dev);
}
