#pragma once
#include "asset_texture.h"
#include "ecs_entity.h"

/**
 * Check if the given asset-id is a normalmap.
 * NOTE: This uses a naming convention based detection (ending with nrm or normal).
 */
bool asset_texture_is_normalmap(String id);

typedef enum {
  AssetTextureType_u8,
  AssetTextureType_u16,
  AssetTextureType_f32,
} AssetTextureType;

/**
 * Create a new texture from the given input pixels.
 *
 * Pre-condition: width > 0
 * Pre-condition: height > 0
 * Pre-condition: channels > 0
 * Pre-condition: layers > 0
 * Pre-condition: mips > 0
 */
AssetTextureComp* asset_texture_create(
    EcsWorld*,
    EcsEntityId,
    Mem in,
    u32 width,
    u32 height,
    u32 channels,
    u32 layers,
    u32 mips,
    AssetTextureType,
    AssetTextureFlags);
