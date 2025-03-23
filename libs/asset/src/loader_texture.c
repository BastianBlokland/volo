#include "core_alloc.h"
#include "core_array.h"
#include "core_bc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_string.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "import_internal.h"
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

DataMeta g_assetTexMeta;

ecs_comp_define_public(AssetTextureComp);
ecs_comp_define_public(AssetTextureSourceComp);

static void ecs_destruct_texture_comp(void* data) {
  AssetTextureComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetTexMeta, mem_create(comp, sizeof(AssetTextureComp)));
}

static void ecs_destruct_texture_source_comp(void* data) {
  AssetTextureSourceComp* comp = data;
  asset_repo_source_close(comp->src);
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
  UNREACHABLE
}

/**
 * Compute how many times we can cut the image in half before both sides hit 1 pixel.
 */
static u16 tex_mips_max(const u32 width, const u32 height) {
  const u16 biggestSide = math_max(width, height);
  const u16 mipCount    = (u16)(32 - bits_clz_32(biggestSide));
  return mipCount;
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

static bool tex_format_bc4x4(const AssetTextureFormat format) {
  switch (format) {
  case AssetTextureFormat_Bc1:
  case AssetTextureFormat_Bc3:
  case AssetTextureFormat_Bc4:
    return true;
  default:
    return false;
  }
}

static u32 tex_format_channels(const AssetTextureFormat format) {
  static const u32 g_channels[AssetTextureFormat_Count] = {
      [AssetTextureFormat_u8_r]     = 1,
      [AssetTextureFormat_u8_rgba]  = 4,
      [AssetTextureFormat_u16_r]    = 1,
      [AssetTextureFormat_u16_rgba] = 4,
      [AssetTextureFormat_f32_r]    = 1,
      [AssetTextureFormat_f32_rgba] = 4,
      [AssetTextureFormat_Bc1]      = 3,
      [AssetTextureFormat_Bc3]      = 4,
      [AssetTextureFormat_Bc4]      = 1,
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
      [AssetTextureFormat_Bc1]      = sizeof(Bc1Block),
      [AssetTextureFormat_Bc3]      = sizeof(Bc3Block),
      [AssetTextureFormat_Bc4]      = sizeof(Bc4Block),
  };
  return g_stride[format];
}

static usize tex_format_mip_size(
    const AssetTextureFormat format,
    const u32                width,
    const u32                height,
    const u32                layers,
    const u32                mip) {
  const u32 mipWidth  = math_max(width >> mip, 1);
  const u32 mipHeight = math_max(height >> mip, 1);
  if (tex_format_bc4x4(format)) {
    const u32 blocks = math_max(mipWidth / 4, 1) * math_max(mipHeight / 4, 1);
    return blocks * tex_format_stride(format) * layers;
  }
  return mipWidth * mipHeight * tex_format_stride(format) * layers;
}

static usize tex_format_size(
    const AssetTextureFormat format,
    const u32                width,
    const u32                height,
    const u32                layers,
    const u32                mips) {
  usize res = 0;
  for (u32 mip = 0; mip != mips; ++mip) {
    res += tex_format_mip_size(format, width, height, layers, mip);
  }
  return res;
}

/**
 * Sample the color at the specified index.
 * NOTE: Does NOT perform any Srgb conversion.
 */
static GeoColor
tex_read_at(const Mem mem, const u32 channels, const AssetTextureType type, const u32 index) {
  diag_assert(mem.size > index * channels * tex_type_size(type));
  static const f32 g_u8MaxInv  = 1.0f / u8_max;
  static const f32 g_u16MaxInv = 1.0f / u16_max;
  GeoColor         color;
  switch (type) {
  case AssetTextureType_u8: {
    const u8* restrict data = mem.ptr;
    color.r                 = data[index * channels + 0] * g_u8MaxInv;
    color.g                 = channels >= 2 ? data[index * channels + 1] * g_u8MaxInv : 0.0f;
    color.b                 = channels >= 3 ? data[index * channels + 2] * g_u8MaxInv : 0.0f;
    color.a                 = channels >= 4 ? data[index * channels + 3] * g_u8MaxInv : 1.0f;
  } break;
  case AssetTextureType_u16: {
    const u16* restrict data = mem.ptr;
    color.r                  = data[index * channels + 0] * g_u16MaxInv;
    color.g                  = channels >= 2 ? data[index * channels + 1] * g_u16MaxInv : 0.0f;
    color.b                  = channels >= 3 ? data[index * channels + 2] * g_u16MaxInv : 0.0f;
    color.a                  = channels >= 4 ? data[index * channels + 3] * g_u16MaxInv : 1.0f;
  } break;
  case AssetTextureType_f32: {
    const f32* restrict data = mem.ptr;
    color.r                  = data[index * channels + 0];
    color.g                  = channels >= 2 ? data[index * channels + 1] : 0.0f;
    color.b                  = channels >= 3 ? data[index * channels + 2] : 0.0f;
    color.a                  = channels >= 4 ? data[index * channels + 3] : 1.0f;
  } break;
  }
  return color;
}

/**
 * Write the color at the specified index.
 * NOTE: Does NOT perform any Srgb conversion.
 */
static void tex_write_at(
    const Mem              mem,
    const u32              channels,
    const AssetTextureType type,
    const u32              index,
    const GeoColor         color) {
  diag_assert(mem.size > index * channels * tex_type_size(type));
  static const f32 g_u8MaxPlusOneRoundDown  = 255.999f;
  static const f32 g_u16MaxPlusOneRoundDown = 65535.999f;
  switch (type) {
  case AssetTextureType_u8: {
    u8* restrict data = mem.ptr;
    switch (channels) {
    case 4:
      data[index * channels + 3] = (u8)(color.a * g_u8MaxPlusOneRoundDown);
    case 3:
      data[index * channels + 2] = (u8)(color.b * g_u8MaxPlusOneRoundDown);
    case 2:
      data[index * channels + 1] = (u8)(color.g * g_u8MaxPlusOneRoundDown);
    case 1:
      data[index * channels + 0] = (u8)(color.r * g_u8MaxPlusOneRoundDown);
    }
  } break;
  case AssetTextureType_u16: {
    u16* restrict data = mem.ptr;
    switch (channels) {
    case 4:
      data[index * channels + 3] = (u16)(color.a * g_u16MaxPlusOneRoundDown);
    case 3:
      data[index * channels + 2] = (u16)(color.b * g_u16MaxPlusOneRoundDown);
    case 2:
      data[index * channels + 1] = (u16)(color.g * g_u16MaxPlusOneRoundDown);
    case 1:
      data[index * channels + 0] = (u16)(color.r * g_u16MaxPlusOneRoundDown);
    }
  } break;
  case AssetTextureType_f32: {
    f32* restrict data = mem.ptr;
    switch (channels) {
    case 4:
      data[index * channels + 3] = color.a;
    case 3:
      data[index * channels + 2] = color.b;
    case 2:
      data[index * channels + 1] = color.g;
    case 1:
      data[index * channels + 0] = color.r;
    }
  } break;
  }
}

static bool tex_can_compress_u8(const u32 width, const u32 height) {
  if (!bits_ispow2_32(width) || !bits_ispow2_32(height)) {
    /**
     * Requiring both sides to be powers of two makes mip-map generation easier as all levels are
     * neatly divisible by four, and then the only needed exceptions are the last levels that are
     * smaller then 4 pixels.
     */
    return false;
  }
  if (width < 4 || height < 4) {
    /**
     * At least 4x4 pixels are needed for block compression, in theory we could add padding but for
     * these tiny sizes its probably not worth it.
     */
    return false;
  }
  return true;
}

static AssetTextureFormat tex_format_pick(
    const AssetTextureType type,
    const u32              width,
    const u32              height,
    const u32              channels,
    const bool             hasAlpha,
    const bool             lossless) {
  switch (type) {
  case AssetTextureType_u8: {
    const bool compress = !lossless && tex_can_compress_u8(width, height);
    if (channels <= 1) {
      return compress ? AssetTextureFormat_Bc4 : AssetTextureFormat_u8_r;
    }
    if (channels <= 3 || !hasAlpha) {
      return compress ? AssetTextureFormat_Bc1 : AssetTextureFormat_u8_rgba;
    }
    return compress ? AssetTextureFormat_Bc3 : AssetTextureFormat_u8_rgba;
  }
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

static BcColor8888 tex_bc0_color_avg(
    const BcColor8888 a, const BcColor8888 b, const BcColor8888 c, const BcColor8888 d) {
  return (BcColor8888){
      (a.r + b.r + c.r + d.r) / 4,
      (a.g + b.g + c.g + d.g) / 4,
      (a.b + b.b + c.b + d.b) / 4,
      (a.a + b.a + c.a + d.a) / 4,
  };
}

static usize tex_bc_encode_block(const Bc0Block* b, const AssetTextureFormat fmt, u8* outPtr) {
  switch (fmt) {
  case AssetTextureFormat_Bc1:
    bc1_encode(b, (Bc1Block*)outPtr);
    return sizeof(Bc1Block);
  case AssetTextureFormat_Bc3:
    bc3_encode(b, (Bc3Block*)outPtr);
    return sizeof(Bc3Block);
  case AssetTextureFormat_Bc4:
    bc4_encode(b, (Bc4Block*)outPtr);
    return sizeof(Bc4Block);
  default:
    diag_crash();
  }
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
  diag_assert(inLayers <= tex->layers && inMips <= tex->mipsData);

  const u8* restrict inPtr = in.ptr;
  diag_assert(in.size == tex_pixel_count(tex->width, tex->height, inLayers, inMips) * inChannels);

  const u32 outChannels = tex_format_channels(tex->format);
  u8* restrict outPtr   = tex->pixelData.ptr;

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

static void tex_load_u8_compress(
    AssetTextureComp* tex,
    const Mem         in,
    const u32         inChannels,
    const u32         inLayers,
    const u32         inMips) {

  diag_assert(inLayers == tex->layers);
  diag_assert(tex_format_bc4x4(tex->format));
  diag_assert(!(tex->flags & AssetTextureFlags_Lossless));
  diag_assert(bits_aligned(tex->width, 4));
  diag_assert(bits_aligned(tex->height, 4));

  const u8* restrict inPtr = in.ptr;
  diag_assert(in.size == tex_pixel_count(tex->width, tex->height, inLayers, inMips) * inChannels);

  u8* restrict outPtr = tex->pixelData.ptr;

  Bc0Block block;
  for (u32 mip = 0; mip != inMips; ++mip) {
    const u32 mipWidth  = math_max(tex->width >> mip, 1);
    const u32 mipHeight = math_max(tex->height >> mip, 1);
    for (u32 l = 0; l != inLayers; ++l) {
      for (u32 y = 0; y < mipHeight; y += 4, inPtr += mipWidth * 4 * inChannels) {
        for (u32 x = 0; x < mipWidth; x += 4) {
          bc0_extract(inPtr + x * inChannels, inChannels, mipWidth, &block);
          outPtr += tex_bc_encode_block(&block, tex->format, outPtr);
        }
      }
    }
  }
  diag_assert(mem_from_to(in.ptr, inPtr).size == in.size);
}

static void tex_load_u8_compress_gen_mips(
    AssetTextureComp* tex,
    const Mem         in,
    const u32         inChannels,
    const u32         inLayers,
    const u32         inMips) {
  (void)inMips;

  diag_assert(inMips <= 1); // Cannot both generate mips and have source mips.
  diag_assert(inLayers == tex->layers);
  diag_assert(tex_format_bc4x4(tex->format));
  diag_assert(!(tex->flags & AssetTextureFlags_Lossless));
  diag_assert(bits_aligned(tex->width, 4) && bits_ispow2_32(tex->width));
  diag_assert(bits_aligned(tex->height, 4) && bits_ispow2_32(tex->height));

  const u8* restrict inPtr = in.ptr;
  diag_assert(in.size == tex_pixel_count(tex->width, tex->height, inLayers, inMips) * inChannels);

  u8* restrict outPtr = tex->pixelData.ptr;

  const u32   layerBlockCount = (tex->width / 4) * (tex->height / 4);
  const usize blockBufferSize = inLayers * layerBlockCount * sizeof(Bc0Block);
  const Mem   blockBuffer     = alloc_alloc(g_allocHeap, blockBufferSize, alignof(Bc0Block));

  Bc0Block* blockPtr = blockBuffer.ptr;

  // Extract 4x4 blocks from the source data and encode mip0.
  for (u32 l = 0; l != inLayers; ++l) {
    for (u32 y = 0; y < tex->height; y += 4, inPtr += tex->width * 4 * inChannels) {
      for (u32 x = 0; x < tex->width; x += 4, ++blockPtr) {
        bc0_extract(inPtr + x * inChannels, inChannels, tex->width, blockPtr);
        outPtr += tex_bc_encode_block(blockPtr, tex->format, outPtr);
      }
    }
  }

  // Down-sample and encode the other mips.
  for (u32 mip = 1; mip < tex->mipsMax; ++mip) {
    blockPtr              = blockBuffer.ptr; // Reset the block pointer to the beginning.
    const u32 blockCountX = math_max((tex->width >> mip) / 4, 1);
    const u32 blockCountY = math_max((tex->height >> mip) / 4, 1);
    for (u32 l = 0; l != inLayers; ++l) {
      for (u32 blockY = 0; blockY != blockCountY; ++blockY) {
        for (u32 blockX = 0; blockX != blockCountX; ++blockX) {
          Bc0Block block;
          // Fill the 4x4 block by down-sampling from 4 blocks of the previous mip.
          for (u32 y = 0; y != 4; ++y) {
            for (u32 x = 0; x != 4; ++x) {
              const u32       srcBlockY = blockY * 2 + (y >= 2);
              const u32       srcBlockX = blockX * 2 + (x >= 2);
              const Bc0Block* src       = &blockPtr[srcBlockY * blockCountX * 2 + srcBlockX];
              const u32       srcX      = (x % 2) * 2;
              const u32       srcY      = (y % 2) * 2;

              const BcColor8888 c0 = src->colors[srcY * 4 + srcX];
              const BcColor8888 c1 = src->colors[srcY * 4 + srcX + 1];
              const BcColor8888 c2 = src->colors[(srcY + 1) * 4 + srcX];
              const BcColor8888 c3 = src->colors[(srcY + 1) * 4 + srcX + 1];

              block.colors[y * 4 + x] = tex_bc0_color_avg(c0, c1, c2, c3);
            }
          }
          // Save the down-sampled block for use in the next mip.
          blockPtr[blockY * blockCountX + blockX] = block;
          // Encode and output this block.
          outPtr += tex_bc_encode_block(&block, tex->format, outPtr);
        }
      }
      blockPtr += layerBlockCount;
    }
  }

  alloc_free(g_allocHeap, blockBuffer);
  diag_assert(mem_from_to(in.ptr, inPtr).size == in.size);
}

static void tex_load_u16(
    AssetTextureComp* tex,
    const Mem         in,
    const u32         inChannels,
    const u32         inLayers,
    const u32         inMips) {
  diag_assert(inLayers <= tex->layers && inMips <= tex->mipsData);

  const usize pixelCount = tex_pixel_count(tex->width, tex->height, inLayers, inMips);
  (void)pixelCount;

  const u16* restrict inPtr = in.ptr;
  diag_assert(in.size == pixelCount * sizeof(u16) * inChannels);

  const u32 outChannels = tex_format_channels(tex->format);
  u16* restrict outPtr  = tex->pixelData.ptr;

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
  diag_assert(inLayers <= tex->layers && inMips <= tex->mipsData);

  const usize pixelCount = tex_pixel_count(tex->width, tex->height, inLayers, inMips);
  (void)pixelCount;

  const f32* restrict inPtr = in.ptr;
  diag_assert(in.size == pixelCount * sizeof(f32) * inChannels);

  const u32 outChannels = tex_format_channels(tex->format);
  f32* restrict outPtr  = tex->pixelData.ptr;

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
    ecs_utils_maybe_remove_t(world, entity, AssetTextureSourceComp);
  }
}

ecs_module_init(asset_texture_module) {
  ecs_register_comp(AssetTextureComp, .destructor = ecs_destruct_texture_comp);
  ecs_register_comp(AssetTextureSourceComp, .destructor = ecs_destruct_texture_source_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadTextureAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_tex(void) {
  // clang-format off
  data_reg_enum_t(g_dataReg, AssetTextureFormat);
  data_reg_const_t(g_dataReg, AssetTextureFormat, u8_r);
  data_reg_const_t(g_dataReg, AssetTextureFormat, u8_rgba);
  data_reg_const_t(g_dataReg, AssetTextureFormat, u16_r);
  data_reg_const_t(g_dataReg, AssetTextureFormat, u16_rgba);
  data_reg_const_t(g_dataReg, AssetTextureFormat, f32_r);
  data_reg_const_t(g_dataReg, AssetTextureFormat, f32_rgba);
  data_reg_const_t(g_dataReg, AssetTextureFormat, Bc1);
  data_reg_const_t(g_dataReg, AssetTextureFormat, Bc3);
  data_reg_const_t(g_dataReg, AssetTextureFormat, Bc4);

  data_reg_enum_multi_t(g_dataReg, AssetTextureFlags);
  data_reg_const_t(g_dataReg, AssetTextureFlags, Srgb);
  data_reg_const_t(g_dataReg, AssetTextureFlags, GenerateMips);
  data_reg_const_t(g_dataReg, AssetTextureFlags, CubeMap);
  data_reg_const_t(g_dataReg, AssetTextureFlags, Alpha);
  data_reg_const_t(g_dataReg, AssetTextureFlags, Lossless);
  data_reg_const_t(g_dataReg, AssetTextureFlags, BroadcastR);

  data_reg_struct_t(g_dataReg, AssetTextureComp);
  data_reg_field_t(g_dataReg, AssetTextureComp, format, t_AssetTextureFormat);
  data_reg_field_t(g_dataReg, AssetTextureComp, flags, t_AssetTextureFlags, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetTextureComp, width, data_prim_t(u32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetTextureComp, height, data_prim_t(u32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetTextureComp, layers, data_prim_t(u32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetTextureComp, mipsData, data_prim_t(u32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetTextureComp, mipsMax, data_prim_t(u32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetTextureComp, pixelData, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);
  // clang-format on

  g_assetTexMeta = data_meta_t(t_AssetTextureComp);
}

void asset_load_tex_bin(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  AssetTextureComp tex;
  DataReadResult   result;
  data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetTexMeta, mem_var(tex), &result);

  if (UNLIKELY(result.error)) {
    log_e(
        "Failed to load binary texture",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error-code", fmt_int(result.error)),
        log_param("error", fmt_text(result.errorMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);
    asset_repo_source_close(src);
    return;
  }

  *ecs_world_add_t(world, entity, AssetTextureComp) = tex;
  ecs_world_add_t(world, entity, AssetTextureSourceComp, .src = src);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
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

Mem asset_texture_data(const AssetTextureComp* t) { return data_mem(t->pixelData); }

GeoColor asset_texture_at(const AssetTextureComp* t, const u32 layer, const usize index) {
  const usize offsetMip0 = tex_format_mip_size(t->format, t->width, t->height, layer, 0);
  const void* pixelsMip0 = bits_ptr_offset(t->pixelData.ptr, offsetMip0);

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
    goto Ret;
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
    goto Ret;
  case AssetTextureFormat_u16_r:
    res.r = ((const u16*)pixelsMip0)[index] * g_u16MaxInv;
    res.g = 0.0f;
    res.b = 0.0f;
    res.a = 1.0f;
    goto Ret;
  case AssetTextureFormat_u16_rgba:
    res.r = ((const u16*)pixelsMip0)[index * 4 + 0] * g_u16MaxInv;
    res.g = ((const u16*)pixelsMip0)[index * 4 + 1] * g_u16MaxInv;
    res.b = ((const u16*)pixelsMip0)[index * 4 + 2] * g_u16MaxInv;
    res.a = ((const u16*)pixelsMip0)[index * 4 + 3] * g_u16MaxInv;
    goto Ret;
  case AssetTextureFormat_f32_r:
    res.r = ((const f32*)pixelsMip0)[index];
    res.g = 0.0f;
    res.b = 0.0f;
    res.a = 1.0f;
    goto Ret;
  case AssetTextureFormat_f32_rgba:
    res.r = ((const f32*)pixelsMip0)[index * 4 + 0];
    res.g = ((const f32*)pixelsMip0)[index * 4 + 1];
    res.b = ((const f32*)pixelsMip0)[index * 4 + 2];
    res.a = ((const f32*)pixelsMip0)[index * 4 + 3];
    goto Ret;
  case AssetTextureFormat_Bc1:
  case AssetTextureFormat_Bc3:
  case AssetTextureFormat_Bc4: {
    const usize pixelX = index % t->width, pixelY = index / t->width;
    const usize blockX = pixelX / 4, blockY = pixelY / 4;
    const usize blockIndex   = blockY * (t->width / 4) + blockX;
    const usize indexInBlock = (pixelY % 4) * 4 + (pixelX % 4);

    Bc0Block blockBc0;
    switch (t->format) {
    case AssetTextureFormat_Bc1:
      bc1_decode((const Bc1Block*)pixelsMip0 + blockIndex, &blockBc0);
      break;
    case AssetTextureFormat_Bc3:
      bc3_decode((const Bc3Block*)pixelsMip0 + blockIndex, &blockBc0);
      break;
    case AssetTextureFormat_Bc4:
    default:
      bc4_decode((const Bc4Block*)pixelsMip0 + blockIndex, &blockBc0);
      break;
    }

    if (t->flags & AssetTextureFlags_Srgb) {
      res.r = g_textureSrgbToFloat[blockBc0.colors[indexInBlock].r];
      res.g = g_textureSrgbToFloat[blockBc0.colors[indexInBlock].g];
      res.b = g_textureSrgbToFloat[blockBc0.colors[indexInBlock].b];
      res.a = blockBc0.colors[indexInBlock].a * g_u8MaxInv;
    } else {
      res.r = blockBc0.colors[indexInBlock].r * g_u8MaxInv;
      res.g = blockBc0.colors[indexInBlock].g * g_u8MaxInv;
      res.b = blockBc0.colors[indexInBlock].b * g_u8MaxInv;
      res.a = blockBc0.colors[indexInBlock].a * g_u8MaxInv;
    }
    goto Ret;
  }
  case AssetTextureFormat_Count:
    break;
  }
  UNREACHABLE

Ret:
  if (t->flags & AssetTextureFlags_BroadcastR) {
    res.g = res.r;
    res.b = res.r;
    res.a = res.r;
  }
  return res;
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

void asset_texture_convert(
    const Mem              srcMem,
    const u32              srcWidth,
    const u32              srcHeight,
    const u32              srcChannels,
    const AssetTextureType srcType,
    const Mem              dstMem,
    const u32              dstWidth,
    const u32              dstHeight,
    const u32              dstChannels,
    const AssetTextureType dstType) {
  diag_assert(
      srcMem.size == asset_texture_type_size(srcType, srcChannels, srcWidth, srcHeight, 1, 1));
  diag_assert(
      dstMem.size == asset_texture_type_size(dstType, dstChannels, dstWidth, dstHeight, 1, 1));

  if (srcWidth == dstWidth && srcHeight == dstHeight) {
    // Identical size; no interpolation necessary just resample the pixel.
    for (u32 i = 0; i != srcHeight * srcWidth; ++i) {
      tex_write_at(dstMem, dstChannels, dstType, i, tex_read_at(srcMem, srcChannels, srcType, i));
    }
    return;
  }

  /**
   * Bilinear interpolation + pixel resampling.
   */

  const f32 xScale = (f32)(srcWidth - 1) / (f32)dstWidth;
  const f32 yScale = (f32)(srcHeight - 1) / (f32)dstHeight;

  for (u32 dstY = 0; dstY != dstHeight; ++dstY) {
    for (u32 dstX = 0; dstX != dstWidth; ++dstX) {
      const u32 srcX   = (u32)(xScale * dstX);
      const u32 srcY   = (u32)(yScale * dstY);
      const u32 srcIdx = (srcY * srcWidth + srcX);

      const GeoColor c1 = tex_read_at(srcMem, srcChannels, srcType, srcIdx);
      const GeoColor c2 = tex_read_at(srcMem, srcChannels, srcType, srcIdx + 1);
      const GeoColor c3 = tex_read_at(srcMem, srcChannels, srcType, srcIdx + srcWidth);
      const GeoColor c4 = tex_read_at(srcMem, srcChannels, srcType, srcIdx + srcWidth + 1);

      const f32 xFrac = (xScale * dstX) - srcX;
      const f32 yFrac = (yScale * dstY) - srcY;

      const GeoColor pixel = geo_color_bilerp(c1, c2, c3, c4, xFrac, yFrac);
      tex_write_at(dstMem, dstChannels, dstType, dstY * dstWidth + dstX, pixel);
    }
  }
}

void asset_texture_transform(
    const Mem              mem,
    const u32              width,
    const u32              height,
    const u32              channels,
    const AssetTextureType type,
    AssetTextureTransform  transform,
    const void*            transformCtx) {

  const u32 pixelCount = width * height;
  for (u32 i = 0; i != pixelCount; ++i) {
    const GeoColor pixel            = tex_read_at(mem, channels, type, i);
    const GeoColor pixelTransformed = transform(transformCtx, pixel);
    tex_write_at(mem, channels, type, i, pixelTransformed);
  }
}

void asset_texture_flip_y(
    const Mem              mem,
    const u32              width,
    const u32              height,
    const u32              channels,
    const AssetTextureType type) {
  const u32 rowSize   = width * channels * tex_type_size(type);
  const Mem rowBuffer = alloc_alloc(g_allocScratch, rowSize, 1);
  for (u32 y = 0; y != (height / 2); ++y) {
    const Mem rowA = mem_slice(mem, y * rowSize, rowSize);
    const Mem rowB = mem_slice(mem, (height - y - 1) * rowSize, rowSize);

    mem_cpy(rowBuffer, rowA);
    mem_cpy(rowA, rowB);
    mem_cpy(rowB, rowBuffer);
  }
}

AssetTextureComp asset_texture_create(
    const Mem              in,
    const u32              width,
    const u32              height,
    const u32              channels,
    const u32              layers,
    const u32              mipsSrc,
    u32                    mipsMax,
    const AssetTextureType type,
    AssetTextureFlags      flags) {
  diag_assert(width && height && channels && layers && mipsSrc);

  if (UNLIKELY(flags & AssetTextureFlags_Srgb && channels < 3)) {
    diag_crash_msg("Srgb requires at least 3 channels");
  }
  if (UNLIKELY(flags & AssetTextureFlags_CubeMap && layers != 6)) {
    diag_crash_msg("CubeMap requires 6 layers");
  }

  const bool alpha    = tex_has_alpha(in, width, height, channels, layers, mipsSrc, type);
  const bool lossless = (flags & AssetTextureFlags_Lossless) != 0;

  if (alpha) {
    flags |= AssetTextureFlags_Alpha;
  } else {
    flags &= ~AssetTextureFlags_Alpha;
  }
  if (channels < 3) {
    flags &= ~AssetTextureFlags_Srgb;
  }
  if (mipsSrc > 1) {
    /**
     * Cannot both generate mips and have source mips.
     */
    flags &= ~AssetTextureFlags_GenerateMips;
  }

  const AssetTextureFormat format = tex_format_pick(type, width, height, channels, alpha, lossless);
  const bool               compress = tex_format_bc4x4(format);
  if (!compress) {
    flags |= AssetTextureFlags_Lossless;
  }

  bool cpuGenMips = false;
  if (flags & AssetTextureFlags_GenerateMips) {
    if (mipsMax) {
      diag_assert(mipsMax <= tex_mips_max(width, height));
    } else {
      mipsMax = tex_mips_max(width, height);
    }

    /**
     * Generate mip-maps on the cpu side for compressed textures; for uncompressed texture the
     * renderer can generate them on the gpu.
     */
    cpuGenMips = compress && mipsSrc == 1;
  } else {
    mipsMax = mipsSrc;
  }

  const u32   dataMips  = cpuGenMips ? mipsMax : mipsSrc;
  const usize dataSize  = tex_format_size(format, width, height, layers, dataMips);
  const usize dataAlign = tex_format_stride(format);
  const Mem   data      = alloc_alloc(g_allocHeap, dataSize, dataAlign);

  AssetTextureComp tex = {
      .format    = format,
      .flags     = flags,
      .width     = width,
      .height    = height,
      .pixelData = data_mem_create(data),
      .layers    = layers,
      .mipsData  = dataMips,
      .mipsMax   = mipsMax,
  };

  switch (type) {
  case AssetTextureType_u8:
    if (compress && cpuGenMips) {
      tex_load_u8_compress_gen_mips(&tex, in, channels, layers, mipsSrc);
    } else if (compress) {
      tex_load_u8_compress(&tex, in, channels, layers, mipsSrc);
    } else {
      tex_load_u8(&tex, in, channels, layers, mipsSrc);
    }
    break;
  case AssetTextureType_u16:
    tex_load_u16(&tex, in, channels, layers, mipsSrc);
    break;
  case AssetTextureType_f32:
    tex_load_f32(&tex, in, channels, layers, mipsSrc);
    break;
  default:
    diag_crash();
  }

  return tex;
}
