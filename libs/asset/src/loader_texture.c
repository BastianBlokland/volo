#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_string.h"
#include "ecs_world.h"
#include "geo_vector.h"

#include "loader_texture_internal.h"
#include "repo_internal.h"

static const f32 g_textureSrgbToFloat[] = {
    0.0f,          0.000303527f, 0.000607054f, 0.00091058103f, 0.001214108f, 0.001517635f,
    0.0018211621f, 0.002124689f, 0.002428216f, 0.002731743f,   0.00303527f,  0.0033465356f,
    0.003676507f,  0.004024717f, 0.004391442f, 0.0047769533f,  0.005181517f, 0.0056053917f,
    0.0060488326f, 0.006512091f, 0.00699541f,  0.0074990317f,  0.008023192f, 0.008568125f,
    0.009134057f,  0.009721218f, 0.010329823f, 0.010960094f,   0.011612245f, 0.012286487f,
    0.012983031f,  0.013702081f, 0.014443844f, 0.015208514f,   0.015996292f, 0.016807375f,
    0.017641952f,  0.018500218f, 0.019382361f, 0.020288562f,   0.02121901f,  0.022173883f,
    0.023153365f,  0.02415763f,  0.025186857f, 0.026241222f,   0.027320892f, 0.028426038f,
    0.029556843f,  0.03071345f,  0.03189604f,  0.033104774f,   0.03433981f,  0.035601325f,
    0.036889452f,  0.038204376f, 0.039546248f, 0.04091521f,    0.042311423f, 0.043735042f,
    0.045186214f,  0.046665095f, 0.048171833f, 0.049706575f,   0.051269468f, 0.052860655f,
    0.05448028f,   0.056128494f, 0.057805434f, 0.05951124f,    0.06124607f,  0.06301003f,
    0.06480328f,   0.06662595f,  0.06847818f,  0.07036011f,    0.07227186f,  0.07421358f,
    0.07618539f,   0.07818743f,  0.08021983f,  0.082282715f,   0.084376216f, 0.086500466f,
    0.088655606f,  0.09084173f,  0.09305898f,  0.095307484f,   0.09758736f,  0.09989874f,
    0.10224175f,   0.10461649f,  0.10702311f,  0.10946172f,    0.111932434f, 0.11443538f,
    0.116970696f,  0.11953845f,  0.12213881f,  0.12477186f,    0.12743773f,  0.13013652f,
    0.13286836f,   0.13563336f,  0.13843165f,  0.14126332f,    0.1441285f,   0.1470273f,
    0.14995982f,   0.15292618f,  0.1559265f,   0.15896086f,    0.16202943f,  0.16513224f,
    0.16826946f,   0.17144115f,  0.17464745f,  0.17788847f,    0.1811643f,   0.18447503f,
    0.1878208f,    0.19120172f,  0.19461787f,  0.19806935f,    0.2015563f,   0.20507877f,
    0.2086369f,    0.21223079f,  0.21586053f,  0.21952623f,    0.22322798f,  0.22696589f,
    0.23074007f,   0.23455065f,  0.23839766f,  0.2422812f,     0.2462014f,   0.25015837f,
    0.25415218f,   0.2581829f,   0.26225072f,  0.26635566f,    0.27049786f,  0.27467737f,
    0.27889434f,   0.2831488f,   0.2874409f,   0.2917707f,     0.29613832f,  0.30054384f,
    0.30498737f,   0.30946895f,  0.31398875f,  0.31854683f,    0.32314324f,  0.32777813f,
    0.33245158f,   0.33716366f,  0.34191445f,  0.3467041f,     0.3515327f,   0.35640025f,
    0.36130688f,   0.3662527f,   0.37123778f,  0.37626222f,    0.3813261f,   0.38642952f,
    0.39157256f,   0.3967553f,   0.40197787f,  0.4072403f,     0.4125427f,   0.41788515f,
    0.42326775f,   0.42869055f,  0.4341537f,   0.43965724f,    0.44520125f,  0.45078585f,
    0.45641106f,   0.46207705f,  0.46778384f,  0.47353154f,    0.47932023f,  0.48514998f,
    0.4910209f,    0.49693304f,  0.5028866f,   0.50888145f,    0.5149178f,   0.5209957f,
    0.52711535f,   0.5332766f,   0.5394797f,   0.5457247f,     0.5520116f,   0.5583406f,
    0.5647117f,    0.57112503f,  0.57758063f,  0.5840786f,     0.590619f,    0.597202f,
    0.60382754f,   0.61049575f,  0.61720675f,  0.62396055f,    0.63075733f,  0.637597f,
    0.6444799f,    0.6514058f,   0.65837497f,  0.66538745f,    0.67244333f,  0.6795426f,
    0.68668544f,   0.69387203f,  0.70110214f,  0.70837605f,    0.7156938f,   0.72305536f,
    0.730461f,     0.7379107f,   0.7454045f,   0.75294244f,    0.76052475f,  0.7681514f,
    0.77582246f,   0.78353804f,  0.79129815f,  0.79910296f,    0.8069525f,   0.8148468f,
    0.822786f,     0.8307701f,   0.83879924f,  0.84687346f,    0.8549928f,   0.8631574f,
    0.87136734f,   0.8796226f,   0.8879232f,   0.89626956f,    0.90466136f,  0.913099f,
    0.92158204f,   0.93011117f,  0.9386859f,   0.9473069f,     0.9559735f,   0.9646866f,
    0.9734455f,    0.98225087f,  0.9911022f,   1.0f,
};
ASSERT(array_elems(g_textureSrgbToFloat) == 256, "Incorrect srgb lut size");

ecs_comp_define_public(AssetTextureComp);

static void ecs_destruct_texture_comp(void* data) {
  AssetTextureComp* comp = data;
  alloc_free(g_allocHeap, asset_texture_data(comp));
}

static u32 tex_type_size(const AssetTextureType type) {
  switch (type) {
  case AssetTextureType_u8:
    return sizeof(u8);
  case AssetTextureType_u16:
    return sizeof(u16);
  case AssetTextureType_f32:
    return sizeof(f32);
  }
  diag_crash();
}

static u32 tex_pixel_count_mip(const u32 width, const u32 height, const u32 layers, const u32 mip) {
  const u32 mipWidth  = math_max(width >> mip, 1);
  const u32 mipHeight = math_max(height >> mip, 1);
  return mipWidth * mipHeight * layers;
}

static u32 tex_pixel_count(const u32 width, const u32 height, const u32 layers, const u32 mips) {
  u32 pixels = 0;
  for (u32 mip = 0; mip != mips; ++mip) {
    pixels += tex_pixel_count_mip(width, height, layers, mip);
  }
  return pixels;
}

static u32 tex_format_channels(const AssetTextureFormat format) {
  static const u32 g_channels[AssetTextureFormat_Count] = {
      [AssetTextureFormat_u8_r]     = 1,
      [AssetTextureFormat_u8_rgba]  = 4,
      [AssetTextureFormat_u16_r]    = 1,
      [AssetTextureFormat_u16_rgba] = 4,
      [AssetTextureFormat_f32_r]    = 1,
      [AssetTextureFormat_f32_rgba] = 4,
  };
  return g_channels[format];
}

static usize tex_format_stride(const AssetTextureFormat format) {
  static const usize g_stride[AssetTextureFormat_Count] = {
      [AssetTextureFormat_u8_r]     = sizeof(u8) * 1,
      [AssetTextureFormat_u8_rgba]  = sizeof(u8) * 4,
      [AssetTextureFormat_u16_r]    = sizeof(u16) * 1,
      [AssetTextureFormat_u16_rgba] = sizeof(u16) * 4,
      [AssetTextureFormat_f32_r]    = sizeof(f32) * 1,
      [AssetTextureFormat_f32_rgba] = sizeof(f32) * 4,
  };
  return g_stride[format];
}

static AssetTextureFormat tex_format_pick(const AssetTextureType type, const u32 channels) {
  switch (type) {
  case AssetTextureType_u8:
    return channels <= 1 ? AssetTextureFormat_u8_r : AssetTextureFormat_u8_rgba;
  case AssetTextureType_u16:
    return channels <= 1 ? AssetTextureFormat_u16_r : AssetTextureFormat_u16_rgba;
  case AssetTextureType_f32:
    return channels <= 1 ? AssetTextureFormat_f32_r : AssetTextureFormat_f32_rgba;
  }
  diag_crash();
}

static bool tex_has_alpha(
    const Mem              in,
    const u32              inWidth,
    const u32              inHeight,
    const u32              inChannels,
    const u32              inLayers,
    const u32              inMips,
    const AssetTextureType inType) {
  if (inChannels < 4) {
    return false;
  }
  const void* inPtr    = in.ptr;
  const u32   inStride = inChannels * tex_type_size(inType);

  static const f32 g_f32AlphaThreshold = 1.0f - f32_epsilon;

  for (u32 mip = 0; mip != inMips; ++mip) {
    const u32 mipWidth  = math_max(inWidth >> mip, 1);
    const u32 mipHeight = math_max(inHeight >> mip, 1);
    for (u32 l = 0; l != inLayers; ++l) {
      for (u32 y = 0; y != mipHeight; ++y) {
        for (u32 x = 0; x < mipWidth; ++x) {
          switch (inType) {
          case AssetTextureType_u8:
            if (((const u8*)inPtr)[3] != u8_max) {
              return true;
            }
            break;
          case AssetTextureType_u16:
            if (((const u16*)inPtr)[3] != u16_max) {
              return true;
            }
            break;
          case AssetTextureType_f32:
            if (((const f32*)inPtr)[3] >= g_f32AlphaThreshold) {
              return true;
            }
            break;
          }
          inPtr = bits_ptr_offset(inPtr, inStride);
        }
      }
    }
  }
  return false;
}

/**
 * The following load utils use the same to RGBA conversion rules as the Vulkan spec:
 * https://registry.khronos.org/vulkan/specs/1.0/html/chap16.html#textures-conversion-to-rgba
 */

static void tex_load_u8(
    AssetTextureComp* tex,
    const Mem         in,
    const u32         inChannels,
    const u32         inLayers,
    const u32         inMips) {
  diag_assert(inLayers <= tex->layers && inMips <= tex->srcMipLevels);

  const u8* restrict inPtr = in.ptr;
  diag_assert(in.size == tex_pixel_count(tex->width, tex->height, inLayers, inMips) * inChannels);

  const u32 outChannels = tex_format_channels(tex->format);
  u8* restrict outPtr   = tex->pixelData;

  for (u32 mip = 0; mip != inMips; ++mip) {
    const u32 mipWidth  = math_max(tex->width >> mip, 1);
    const u32 mipHeight = math_max(tex->height >> mip, 1);
    for (u32 l = 0; l != inLayers; ++l) {
      for (u32 y = 0; y != mipHeight; ++y) {
        for (u32 x = 0; x < mipWidth; ++x) {
          switch (tex->format) {
          case AssetTextureFormat_u8_r:
            outPtr[0] = inPtr[0];
            break;
          case AssetTextureFormat_u8_rgba:
            outPtr[0] = inPtr[0];
            outPtr[1] = inChannels >= 2 ? inPtr[1] : 0;
            outPtr[2] = inChannels >= 3 ? inPtr[2] : 0;
            outPtr[3] = inChannels >= 4 ? inPtr[3] : u8_max;
            break;
          default:
            diag_crash();
          }

          inPtr += inChannels;
          outPtr += outChannels;
        }
      }
    }
  }
  diag_assert(mem_from_to(in.ptr, inPtr).size == in.size);
}

static void tex_load_u16(
    AssetTextureComp* tex,
    const Mem         in,
    const u32         inChannels,
    const u32         inLayers,
    const u32         inMips) {
  diag_assert(inLayers <= tex->layers && inMips <= tex->srcMipLevels);

  const u16* restrict inPtr = in.ptr;
  diag_assert(in.size == tex_pixel_count(tex->width, tex->height, inLayers, inMips) * inChannels);

  const u32 outChannels = tex_format_channels(tex->format);
  u16* restrict outPtr  = tex->pixelData;

  for (u32 mip = 0; mip != inMips; ++mip) {
    const u32 mipWidth  = math_max(tex->width >> mip, 1);
    const u32 mipHeight = math_max(tex->height >> mip, 1);
    for (u32 l = 0; l != inLayers; ++l) {
      for (u32 y = 0; y != mipHeight; ++y) {
        for (u32 x = 0; x < mipWidth; ++x) {
          switch (tex->format) {
          case AssetTextureFormat_u16_r:
            outPtr[0] = inPtr[0];
            break;
          case AssetTextureFormat_u16_rgba:
            outPtr[0] = inPtr[0];
            outPtr[1] = inChannels >= 2 ? inPtr[1] : 0;
            outPtr[2] = inChannels >= 3 ? inPtr[2] : 0;
            outPtr[3] = inChannels >= 4 ? inPtr[3] : u16_max;
            break;
          default:
            diag_crash();
          }

          inPtr += inChannels;
          outPtr += outChannels;
        }
      }
    }
  }
  diag_assert(mem_from_to(in.ptr, inPtr).size == in.size);
}

static void tex_load_f32(
    AssetTextureComp* tex,
    const Mem         in,
    const u32         inChannels,
    const u32         inLayers,
    const u32         inMips) {
  diag_assert(inLayers <= tex->layers && inMips <= tex->srcMipLevels);

  const f32* restrict inPtr = in.ptr;
  diag_assert(in.size == tex_pixel_count(tex->width, tex->height, inLayers, inMips) * inChannels);

  const u32 outChannels = tex_format_channels(tex->format);
  f32* restrict outPtr  = tex->pixelData;

  for (u32 mip = 0; mip != inMips; ++mip) {
    const u32 mipWidth  = math_max(tex->width >> mip, 1);
    const u32 mipHeight = math_max(tex->height >> mip, 1);
    for (u32 l = 0; l != inLayers; ++l) {
      for (u32 y = 0; y != mipHeight; ++y) {
        for (u32 x = 0; x < mipWidth; ++x) {
          switch (tex->format) {
          case AssetTextureFormat_f32_r:
            outPtr[0] = inPtr[0];
            break;
          case AssetTextureFormat_f32_rgba:
            outPtr[0] = inPtr[0];
            outPtr[1] = inChannels >= 2 ? inPtr[1] : 0;
            outPtr[2] = inChannels >= 3 ? inPtr[2] : 0;
            outPtr[3] = inChannels >= 4 ? inPtr[3] : 1.0f;
            break;
          default:
            diag_crash();
          }

          inPtr += inChannels;
          outPtr += outChannels;
        }
      }
    }
  }
  diag_assert(mem_from_to(in.ptr, inPtr).size == in.size);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetTextureComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any texture-asset components for unloaded assets.
 */
ecs_system_define(UnloadTextureAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetTextureComp);
  }
}

ecs_module_init(asset_texture_module) {
  ecs_register_comp(AssetTextureComp, .destructor = ecs_destruct_texture_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadTextureAssetSys, ecs_view_id(UnloadView));
}

String asset_texture_format_str(const AssetTextureFormat format) {
  static const String g_names[AssetTextureFormat_Count] = {
      [AssetTextureFormat_u8_r]     = string_static("u8-r"),
      [AssetTextureFormat_u8_rgba]  = string_static("u8-rgba"),
      [AssetTextureFormat_u16_r]    = string_static("u16-r"),
      [AssetTextureFormat_u16_rgba] = string_static("u16-rgba"),
      [AssetTextureFormat_f32_r]    = string_static("f32-r"),
      [AssetTextureFormat_f32_rgba] = string_static("f32-rgba"),
  };
  return g_names[format];
}

usize asset_texture_format_channels(const AssetTextureFormat format) {
  return tex_format_channels(format);
}

usize asset_texture_mip_size(const AssetTextureComp* t, const u32 mipLevel) {
  diag_assert(mipLevel < t->srcMipLevels);
  const u32 count = tex_pixel_count_mip(t->width, t->height, t->layers, mipLevel);
  return count * tex_format_stride(t->format);
}

usize asset_texture_data_size(const AssetTextureComp* t) {
  const u32 count = tex_pixel_count(t->width, t->height, t->layers, t->srcMipLevels);
  return count * tex_format_stride(t->format);
}

Mem asset_texture_data(const AssetTextureComp* t) {
  return mem_create(t->pixelData, asset_texture_data_size(t));
}

GeoColor asset_texture_at(const AssetTextureComp* t, const u32 layer, const usize index) {
  const usize pixelCount    = t->width * t->height;
  const usize layerDataSize = pixelCount * tex_format_stride(t->format);
  const void* pixelsMip0    = bits_ptr_offset(t->pixelData, layerDataSize * layer);

  static const f32 g_u8MaxInv  = 1.0f / u8_max;
  static const f32 g_u16MaxInv = 1.0f / u16_max;

  GeoColor res;
  switch (t->format) {
  case AssetTextureFormat_u8_r:
    if (t->flags & AssetTextureFlags_Srgb) {
      res.r = g_textureSrgbToFloat[((const u8*)pixelsMip0)[index]];
    } else {
      res.r = ((const u8*)pixelsMip0)[index] * g_u8MaxInv;
    }
    res.g = 0.0f;
    res.b = 0.0f;
    res.a = 1.0f;
    return res;
  case AssetTextureFormat_u8_rgba:
    if (t->flags & AssetTextureFlags_Srgb) {
      res.r = g_textureSrgbToFloat[((const u8*)pixelsMip0)[index * 4 + 0]];
      res.g = g_textureSrgbToFloat[((const u8*)pixelsMip0)[index * 4 + 1]];
      res.b = g_textureSrgbToFloat[((const u8*)pixelsMip0)[index * 4 + 2]];
      res.a = ((const u8*)pixelsMip0)[index * 4 + 3] * g_u8MaxInv;
    } else {
      res.r = ((const u8*)pixelsMip0)[index * 4 + 0] * g_u8MaxInv;
      res.g = ((const u8*)pixelsMip0)[index * 4 + 1] * g_u8MaxInv;
      res.b = ((const u8*)pixelsMip0)[index * 4 + 2] * g_u8MaxInv;
      res.a = ((const u8*)pixelsMip0)[index * 4 + 3] * g_u8MaxInv;
    }
    return res;
  case AssetTextureFormat_u16_r:
    res.r = ((const u16*)pixelsMip0)[index] * g_u16MaxInv;
    res.g = 0.0f;
    res.b = 0.0f;
    res.a = 1.0f;
    return res;
  case AssetTextureFormat_u16_rgba:
    res.r = ((const u16*)pixelsMip0)[index * 4 + 0] * g_u16MaxInv;
    res.g = ((const u16*)pixelsMip0)[index * 4 + 1] * g_u16MaxInv;
    res.b = ((const u16*)pixelsMip0)[index * 4 + 2] * g_u16MaxInv;
    res.a = ((const u16*)pixelsMip0)[index * 4 + 3] * g_u16MaxInv;
    return res;
  case AssetTextureFormat_f32_r:
    res.r = ((const f32*)pixelsMip0)[index];
    res.g = 0.0f;
    res.b = 0.0f;
    res.a = 1.0f;
    return res;
  case AssetTextureFormat_f32_rgba:
    res.r = ((const f32*)pixelsMip0)[index * 4 + 0];
    res.g = ((const f32*)pixelsMip0)[index * 4 + 1];
    res.b = ((const f32*)pixelsMip0)[index * 4 + 2];
    res.a = ((const f32*)pixelsMip0)[index * 4 + 3];
    return res;
  case AssetTextureFormat_Count:
    break;
  }
  UNREACHABLE
}

GeoColor
asset_texture_sample(const AssetTextureComp* t, const f32 xNorm, const f32 yNorm, const u32 layer) {
  diag_assert(xNorm >= 0.0 && xNorm <= 1.0f);
  diag_assert(yNorm >= 0.0 && yNorm <= 1.0f);
  diag_assert(layer < t->layers);

  const f32 x = xNorm * (t->width - 1), y = yNorm * (t->height - 1);

  const f32 corner1x = math_min(t->width - 2, math_round_down_f32(x));
  const f32 corner1y = math_min(t->height - 2, math_round_down_f32(y));
  const f32 corner2x = corner1x + 1.0f, corner2y = corner1y + 1.0f;

  const GeoColor c1 = asset_texture_at(t, layer, (usize)corner1y * t->width + (usize)corner1x);
  const GeoColor c2 = asset_texture_at(t, layer, (usize)corner1y * t->width + (usize)corner2x);
  const GeoColor c3 = asset_texture_at(t, layer, (usize)corner2y * t->width + (usize)corner1x);
  const GeoColor c4 = asset_texture_at(t, layer, (usize)corner2y * t->width + (usize)corner2x);

  return geo_color_bilerp(c1, c2, c3, c4, x - corner1x, y - corner1y);
}

GeoColor asset_texture_sample_nearest(
    const AssetTextureComp* t, const f32 xNorm, const f32 yNorm, const u32 layer) {
  diag_assert(xNorm >= 0.0 && xNorm <= 1.0f);
  diag_assert(yNorm >= 0.0 && yNorm <= 1.0f);
  diag_assert(layer < t->layers);

  const usize x = (usize)math_round_nearest_f32(xNorm * (t->width - 1));
  const usize y = (usize)math_round_nearest_f32(yNorm * (t->height - 1));
  return asset_texture_at(t, layer, y * t->width + x);
}

bool asset_texture_is_normalmap(const String id) {
  static const String g_patterns[] = {
      string_static("*_nrm.*"),
      string_static("*_normal.*"),
      string_static("*_nrm_*.*"),
      string_static("*_normal_*.*"),
  };
  array_for_t(g_patterns, String, pattern) {
    if (string_match_glob(id, *pattern, StringMatchFlags_IgnoreCase)) {
      return true;
    }
  }
  return false;
}

usize asset_texture_type_stride(const AssetTextureType type, const u32 channels) {
  return channels * tex_type_size(type);
}

usize asset_texture_type_mip_size(
    const AssetTextureType type,
    const u32              channels,
    const u32              width,
    const u32              height,
    const u32              layers,
    const u32              mip) {
  return tex_pixel_count_mip(width, height, layers, mip) * channels * tex_type_size(type);
}

usize asset_texture_type_size(
    const AssetTextureType type,
    const u32              channels,
    const u32              width,
    const u32              height,
    const u32              layers,
    const u32              mips) {
  return tex_pixel_count(width, height, layers, mips) * channels * tex_type_size(type);
}

AssetTextureComp asset_texture_create(
    const Mem              in,
    const u32              width,
    const u32              height,
    const u32              channels,
    const u32              layers,
    const u32              mips,
    const AssetTextureType type,
    AssetTextureFlags      flags) {
  diag_assert(width && height && channels && layers && mips);

  if (UNLIKELY(flags & AssetTextureFlags_Srgb && channels < 3)) {
    diag_crash_msg("Srgb requires at least 3 channels");
  }
  if (tex_has_alpha(in, width, height, channels, layers, mips, type)) {
    flags |= AssetTextureFlags_Alpha;
  }

  const AssetTextureFormat format     = tex_format_pick(type, channels);
  const usize              dataStride = tex_format_stride(format);
  const usize              dataSize   = tex_pixel_count(width, height, layers, mips) * dataStride;
  const Mem                data       = alloc_alloc(g_allocHeap, dataSize, dataStride);

  AssetTextureComp tex = {
      .format       = format,
      .flags        = flags,
      .width        = width,
      .height       = height,
      .pixelData    = data.ptr,
      .layers       = layers,
      .srcMipLevels = mips,
  };

  switch (type) {
  case AssetTextureType_u8:
    tex_load_u8(&tex, in, channels, layers, mips);
    break;
  case AssetTextureType_u16:
    tex_load_u16(&tex, in, channels, layers, mips);
    break;
  case AssetTextureType_f32:
    tex_load_f32(&tex, in, channels, layers, mips);
    break;
  default:
    diag_crash();
  }

  return tex;
}
