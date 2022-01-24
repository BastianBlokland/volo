#pragma once
#include "ecs_module.h"

typedef enum {
  SceneTag_None = 0,
} SceneTags;

ecs_comp_extern_public(SceneTagComp) { SceneTags tags; };
