#pragma once
#include "asset_texture.h"

#include "repo_internal.h"

ecs_comp_extern_public(AssetTextureSourceComp) { AssetSource* src; };

typedef enum {
  AssetTextureType_u8,
  AssetTextureType_u16,
  AssetTextureType_f32,
} AssetTextureType;

usize asset_texture_type_stride(AssetTextureType, u32 channels);
usize asset_texture_type_mip_size(
    AssetTextureType, u32 channels, u32 width, u32 height, u32 layers, u32 mip);
usize asset_texture_type_size(
    AssetTextureType, u32 channels, u32 width, u32 height, u32 layers, u32 mips);

/**
 * Convert the source pixels to the given size, channels and type.
 *
 * Pre-condition: srcWidth > 0 && dstWidth > 0
 * Pre-condition: srcHeight > 0 && dstHeight > 0
 * Pre-condition: srcChannels > 0 && dstChannels > 0
 */
void asset_texture_convert(
    Mem              srcMem,
    u32              srcWidth,
    u32              srcHeight,
    u32              srcChannels,
    AssetTextureType srcType,
    Mem              dstMem,
    u32              dstWidth,
    u32              dstHeight,
    u32              dstChannels,
    AssetTextureType dstType);

/**
 * In-place flip (mirror) the y axis.
 */
void asset_texture_flip_y(Mem, u32 width, u32 height, u32 channels, AssetTextureType);

typedef GeoColor (*AssetTextureTransform)(const void* ctx, GeoColor);

/**
 * Apply a  color transformation.
 */
void asset_texture_transform(
    Mem,
    u32 width,
    u32 height,
    u32 channels,
    AssetTextureType,
    AssetTextureTransform,
    const void* transformCtx);

/**
 * Create a new texture from the given input pixels.
 *
 * Pre-condition: width > 0
 * Pre-condition: height > 0
 * Pre-condition: channels > 0
 * Pre-condition: layers > 0
 * Pre-condition: mipsSrc > 0
 */
AssetTextureComp asset_texture_create(
    Mem in,
    u32 width,
    u32 height,
    u32 channels,
    u32 layers,
    u32 mipsSrc,
    u32 mipsMax,
    AssetTextureType,
    AssetTextureFlags);
