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

typedef enum {
  SpvError_None = 0,
  SpvError_Malformed,
  SpvError_MalformedIdOutOfBounds,
  SpvError_MalformedDuplicateId,
  SpvError_MalformedResourceWithoutSetAndId,
  SpvError_MalformedDuplicateBinding,
  SpvError_MalformedSpecWithoutBinding,
  SpvError_UnsupportedVersion,
  SpvError_UnsupportedMultipleEntryPoints,
  SpvError_UnsupportedShaderResource,
  SpvError_UnsupportedSpecConstantType,
  SpvError_UnsupportedSetExceedsMax,
  SpvError_UnsupportedBindingExceedsMax,
  SpvError_UnsupportedInputExceedsMax,
  SpvError_UnsupportedOutputExceedsMax,
  SpvError_UnsupportedImageType,
  SpvError_MultipleKillInstructions,
  SpvError_TooManySpecConstBranches,

  SpvError_Count,
} SpvError;

String   spv_err_str(SpvError);
SpvError spv_init(EcsWorld*, EcsEntityId, Mem input);
