#pragma once
#include "core_string.h"

typedef enum {
  RendPass_Geometry,
  RendPass_Decal,
  RendPass_Fog,
  RendPass_FogBlur,
  RendPass_Shadow,
  RendPass_AmbientOcclusion,
  RendPass_Forward,
  RendPass_Distortion,
  RendPass_Bloom,
  RendPass_Post,

  RendPass_Count,
} RendPass;

extern const String g_rendPassNames[RendPass_Count];
