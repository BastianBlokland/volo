#pragma once
#include "core_string.h"

typedef enum {
  RendPass_Geometry,
  RendPass_Forward,
  RendPass_Post,
  RendPass_Shadow,
  RendPass_AmbientOcclusion,
  RendPass_Bloom,
  RendPass_Distortion,
  RendPass_Decal,

  RendPass_Count,
} RendPass;

extern const String g_rendPassNames[RendPass_Count];

String rend_pass_name(RendPass);
