#pragma once
#include "ecs/def.h"

enum {
  RendOrder_ResourceLoad = -1,
  RendOrder_ObjectClear  = -100,
  RendOrder_ObjectUpdate = 900,
  RendOrder_Draw         = 1000,
  RendOrder_FrameLimiter = 2000,
  RendOrder_Reset        = 3000,
};

typedef struct {
  bool enableStats;
} RendRegisterContext;

/**
 * Register the ecs modules for the Renderer library.
 */
void rend_register(EcsDef*, const RendRegisterContext*);
