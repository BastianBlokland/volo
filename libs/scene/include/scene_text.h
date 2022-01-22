#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(SceneTextComp) {
  f32    position[2];
  f32    size;
  String text;
};
