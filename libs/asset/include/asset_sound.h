#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(AssetSoundComp) {
  u32        frameCount;
  u32        frameRate;
  u32        frameChannels;
  const f32* samples; // f32[frameCount * channelCount], Interleaved channel samples (LRLRLR).
};
