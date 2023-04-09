#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(AssetSoundComp) {
  u8         frameChannels;
  u32        frameCount;
  u32        frameRate;
  const f32* samples; // f32[frameCount * channelCount], Interleaved channel samples (LRLRLR).
};
