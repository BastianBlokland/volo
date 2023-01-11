#pragma once
#include "core_string.h"

typedef enum {
  RendPass_Geometry,
  RendPass_Forward,
  RendPass_Post,
  RendPass_Shadow,
  RendPass_AmbientOcclusion,

  RendPass_Count,
} RendPass;

String rend_pass_name(RendPass);
