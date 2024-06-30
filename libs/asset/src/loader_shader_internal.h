#pragma once
#include "asset_shader.h"

#include "repo_internal.h"

typedef enum {
  AssetShaderSource_Repository,
  AssetShaderSource_Memory,
} AssetShaderSourceType;

ecs_comp_extern_public(AssetShaderSourceComp) {
  AssetShaderSourceType type;
  union {
    AssetSource* srcRepo;
    Mem          srcMem;
  };
};

bool asset_init_spv_from_mem(EcsWorld*, EcsEntityId, Mem input);
