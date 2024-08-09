#pragma once
#include "data_registry.h"
#include "ecs_module.h"

ecs_comp_extern_public(AssetSoundComp) {
  u8      frameChannels;
  u32     frameCount;
  u32     frameRate;
  DataMem sampleData; // f32[frameCount * channelCount], Interleaved channel samples (LRLRLR).
};

extern DataMeta g_assetSoundMeta;
