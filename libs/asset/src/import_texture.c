#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_math.h"
#include "script_args.h"
#include "script_binder.h"
#include "script_enum.h"
#include "script_panic.h"
#include "script_sig.h"

#include "import_internal.h"
#include "import_texture_internal.h"

ScriptBinder* g_assetScriptImportTextureBinder;

static ScriptEnum g_importTextureFlags, g_importTexturePixelType;

static void import_init_enum_flags(void) {
#define ENUM_PUSH(_ENUM_, _NAME_)                                                                  \
  script_enum_push((_ENUM_), string_lit(#_NAME_), AssetImportTextureFlags_##_NAME_);

  ENUM_PUSH(&g_importTextureFlags, Lossless);
  ENUM_PUSH(&g_importTextureFlags, Linear);
  ENUM_PUSH(&g_importTextureFlags, Mips);

#undef ENUM_PUSH
}

static void import_init_enum_pixel_type(void) {
#define ENUM_PUSH(_ENUM_, _NAME_)                                                                  \
  script_enum_push((_ENUM_), string_lit(#_NAME_), AssetTextureType_##_NAME_);

  ENUM_PUSH(&g_importTexturePixelType, u8);
  ENUM_PUSH(&g_importTexturePixelType, u16);
  ENUM_PUSH(&g_importTexturePixelType, f32);

#undef ENUM_PUSH
}

static u32 import_texture_type_size(const AssetTextureType type) {
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
static u16 import_texture_mips_max(const u32 width, const u32 height) {
  const u16 biggestSide = math_max(width, height);
  const u16 mipCount    = (u16)(32 - bits_clz_32(biggestSide));
  return mipCount;
}

static f32 import_texture_rem1(const f32 val) {
  const f32 mod = val - (u32)val;
  return mod < 0.0f ? 1.0f + mod : mod;
}

static f32 import_texture_clamp01(const f32 val) {
  if (val <= 0.0f) {
    return 0.0f;
  }
  if (val >= 1.0f) {
    return 1.0f;
  }
  return val;
}

typedef struct {
  AssetImportTextureFlags flags;
  AssetImportTextureFlip  flip;
  u32                     width, height, layers;
  u32                     mips; // 0 indicates maximum number of mips.
  u32                     channels;
  AssetTextureType        type;
  Mem                     data;
  u32                     dataWidth, dataHeight, dataChannels;
  AssetTextureType        dataType;
} AssetImportTexture;

static ScriptVal import_eval_pow2_test(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)ctx;
  const f64 val = script_arg_num(call, 0);
  return script_bool(bits_ispow2_64((u64)val));
}

static ScriptVal import_eval_pow2_next(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)ctx;
  const f64 val = script_arg_num_range(call, 0, 1.0, 9223372036854775807.0);
  return script_num(bits_nextpow2_64((u64)val));
}

static ScriptVal import_eval_texture_channels(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportTexture* data = ctx->data;
  if (call->argCount < 1) {
    return script_num(data->channels);
  }
  const u32 newChannels = (u32)script_arg_num_range(call, 0, 1, 4);
  diag_assert(newChannels >= 1 && newChannels <= 4);
  data->channels = newChannels;
  return script_null();
}

static ScriptVal import_eval_texture_flag(AssetImportContext* ctx, ScriptBinderCall* call) {
  const i32           flag = script_arg_enum(call, 0, &g_importTextureFlags);
  AssetImportTexture* data = ctx->data;
  if (call->argCount < 2) {
    return script_bool((data->flags & flag) != 0);
  }
  const bool enabled = script_arg_bool(call, 1);
  if (enabled != !!(data->flags & flag)) {
    data->flags ^= flag;
  }
  return script_null();
}

static ScriptVal import_eval_texture_type(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportTexture* data = ctx->data;
  if (call->argCount < 1) {
    return script_str(script_enum_lookup_name(&g_importTexturePixelType, data->type));
  }
  data->type = (AssetTextureType)script_arg_enum(call, 0, &g_importTexturePixelType);
  return script_null();
}

static ScriptVal import_eval_texture_width(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  AssetImportTexture* data = ctx->data;
  return script_num(data->width);
}

static ScriptVal import_eval_texture_height(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  AssetImportTexture* data = ctx->data;
  return script_num(data->height);
}

static ScriptVal import_eval_texture_layers(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  AssetImportTexture* data = ctx->data;
  return script_num(data->layers);
}

static ScriptVal import_eval_texture_mips(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportTexture* data = ctx->data;
  if (call->argCount) {
    const u32 mipsMax = import_texture_mips_max(data->width, data->height);
    data->mips        = (u32)script_arg_num_range(call, 0, 0, mipsMax);
    if (data->mips == 1) {
      data->flags &= ~AssetImportTextureFlags_Mips;
    } else {
      data->flags |= AssetImportTextureFlags_Mips;
    }
    return script_null();
  }
  if (data->flags & AssetImportTextureFlags_Mips) {
    u32 res = data->mips;
    if (res) {
      res = math_min(res, import_texture_mips_max(data->width, data->height));
    } else {
      res = import_texture_mips_max(data->width, data->height);
    }
    return script_num(res);
  }
  return script_num(1);
}

static ScriptVal import_eval_texture_mips_max(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  AssetImportTexture* data = ctx->data;
  return script_num(import_texture_mips_max(data->width, data->height));
}

static ScriptVal import_eval_texture_flip_y(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  AssetImportTexture* data = ctx->data;
  data->flip ^= AssetImportTextureFlip_Y;
  return script_null();
}

static ScriptVal import_eval_texture_resize(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportTexture* data = ctx->data;
  data->width              = (u32)script_arg_num_range(call, 0, 1, 1024 * 16);
  data->height             = (u32)script_arg_num_range(call, 1, 1, 1024 * 16);
  return script_null();
}

static GeoColor tex_trans_mul(const void* ctx, const GeoColor color) {
  const GeoColor* ref = ctx;
  return geo_color_clamp01(geo_color_mul_comps(color, *ref));
}

static ScriptVal import_eval_texture_trans_mul(AssetImportContext* ctx, ScriptBinderCall* call) {
  const GeoColor      color = script_arg_color(call, 0);
  AssetImportTexture* data  = ctx->data;
  asset_texture_transform(
      data->data,
      data->dataWidth,
      data->dataHeight,
      data->dataChannels,
      data->dataType,
      tex_trans_mul,
      &color);
  return script_null();
}

static GeoColor tex_trans_add(const void* ctx, const GeoColor color) {
  const GeoColor* ref = ctx;
  return geo_color_clamp01(geo_color_add(color, *ref));
}

static ScriptVal import_eval_texture_trans_add(AssetImportContext* ctx, ScriptBinderCall* call) {
  const GeoColor      color = script_arg_color(call, 0);
  AssetImportTexture* data  = ctx->data;
  asset_texture_transform(
      data->data,
      data->dataWidth,
      data->dataHeight,
      data->dataChannels,
      data->dataType,
      tex_trans_add,
      &color);
  return script_null();
}

static GeoColor tex_trans_sub(const void* ctx, const GeoColor color) {
  const GeoColor* ref = ctx;
  return geo_color_clamp01(geo_color_sub(color, *ref));
}

static ScriptVal import_eval_texture_trans_sub(AssetImportContext* ctx, ScriptBinderCall* call) {
  const GeoColor      color = script_arg_color(call, 0);
  AssetImportTexture* data  = ctx->data;
  asset_texture_transform(
      data->data,
      data->dataWidth,
      data->dataHeight,
      data->dataChannels,
      data->dataType,
      tex_trans_sub,
      &color);
  return script_null();
}

static GeoColor tex_trans_gray(const void* ctx, const GeoColor color) {
  (void)ctx;
  // Rec709 luminance coefficients.
  const f32 luma = color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f;
  return geo_color(luma, luma, luma, color.a);
}

static ScriptVal import_eval_texture_trans_gray(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  AssetImportTexture* data = ctx->data;
  asset_texture_transform(
      data->data,
      data->dataWidth,
      data->dataHeight,
      data->dataChannels,
      data->dataType,
      tex_trans_gray,
      null);
  return script_null();
}

typedef struct {
  f32 hue, saturation, value, alpha;
} TexShiftCtx;

static GeoColor tex_trans_shift(const void* ctx, const GeoColor color) {
  const TexShiftCtx* shiftCtx = ctx;

  f32 hue, saturation, value, alpha;
  geo_color_to_hsv(color, &hue, &saturation, &value, &alpha);

  hue        = import_texture_rem1(hue + shiftCtx->hue);
  saturation = import_texture_clamp01(saturation + shiftCtx->saturation);
  value      = import_texture_clamp01(value + shiftCtx->value);
  alpha      = import_texture_clamp01(alpha + shiftCtx->alpha);

  return geo_color_from_hsv(hue, saturation, value, alpha);
}

static ScriptVal import_eval_texture_trans_shift(AssetImportContext* ctx, ScriptBinderCall* call) {
  const TexShiftCtx shiftCtx = {
      .hue        = (f32)script_arg_num(call, 0),
      .saturation = (f32)script_arg_opt_num(call, 1, 0.0),
      .value      = (f32)script_arg_opt_num(call, 2, 0.0),
      .alpha      = (f32)script_arg_opt_num(call, 3, 0.0),
  };
  AssetImportTexture* data = ctx->data;
  asset_texture_transform(
      data->data,
      data->dataWidth,
      data->dataHeight,
      data->dataChannels,
      data->dataType,
      tex_trans_shift,
      &shiftCtx);
  return script_null();
}

typedef struct {
  f32 old, new;
  f32 threshold, thresholdInv;
} TexReplaceHueCtx;

static GeoColor tex_trans_replace_hue(const void* ctx, const GeoColor color) {
  const TexReplaceHueCtx* replaceCtx = ctx;

  f32 hue, saturation, value, alpha;
  geo_color_to_hsv(color, &hue, &saturation, &value, &alpha);

  const f32 hueDelta = replaceCtx->old - hue;
  const f32 hueDist  = math_abs(hueDelta);
  if (hueDist > replaceCtx->threshold) {
    return color;
  }
  const GeoColor colorNew = geo_color_from_hsv(replaceCtx->new, saturation, value, alpha);

  return geo_color_lerp(color, colorNew, 1.0f - hueDist * replaceCtx->thresholdInv);
}

static ScriptVal
import_eval_texture_trans_replace(AssetImportContext* ctx, ScriptBinderCall* call) {
  TexReplaceHueCtx replaceCtx = {
      .old       = (f32)script_arg_num(call, 0),
      .new       = (f32)script_arg_num(call, 1),
      .threshold = (f32)script_arg_opt_num_range(call, 2, 1e-3, 1.0, 0.1),
  };
  replaceCtx.thresholdInv = 1.0f / replaceCtx.threshold;

  AssetImportTexture* data = ctx->data;
  asset_texture_transform(
      data->data,
      data->dataWidth,
      data->dataHeight,
      data->dataChannels,
      data->dataType,
      tex_trans_replace_hue,
      &replaceCtx);
  return script_null();
}

void asset_data_init_import_texture(void) {
  import_init_enum_flags();
  import_init_enum_pixel_type();

  const ScriptBinderFlags flags = ScriptBinderFlags_DisallowMemoryAccess;
  ScriptBinder* binder = script_binder_create(g_allocPersist, string_lit("import-texture"), flags);
  script_binder_filter_set(binder, string_lit("import/texture/*.script"));

  // clang-format off
  static const String g_flagsDoc     = string_static("Supported flags:\n\n-`Lossless`\n\n-`Linear`\n\n-`Mips`");
  static const String g_pixelTypeDoc = string_static("Supported types:\n\n-`u8`\n\n-`u16`\n\n-`f32`");
  {
    const String       name   = string_lit("pow2_test");
    const String       doc    = string_lit("Check if the given value is a power of two.");
    const ScriptMask   ret    = script_mask_bool;
    const ScriptSigArg args[] = {
        {string_lit("value"), script_mask_num},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_pow2_test);
  }
  {
    const String       name   = string_lit("pow2_next");
    const String       doc    = string_lit("Return the next power of two greater or equal to the given value.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("value"), script_mask_num},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_pow2_next);
  }
  {
    const String       name   = string_lit("texture_channels");
    const String       doc    = string_lit("Query or change the amount of channels in the texture.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("channels"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_channels);
  }
  {
    const String       name   = string_lit("texture_flag");
    const String       doc    = fmt_write_scratch("Query or change a texture import flag.\n\n{}", fmt_text(g_flagsDoc));
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("flag"), script_mask_str},
        {string_lit("enable"), script_mask_bool | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_flag);
  }
  {
    const String       name   = string_lit("texture_type");
    const String       doc    = fmt_write_scratch("Query or change the texture pixel type.\n\n{}", fmt_text(g_pixelTypeDoc));
    const ScriptMask   ret    = script_mask_str;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_texture_type);
  }
  {
    const String       name   = string_lit("texture_width");
    const String       doc    = string_lit("Query the texture width in pixels.");
    const ScriptMask   ret    = script_mask_num;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_texture_width);
  }
  {
    const String       name   = string_lit("texture_height");
    const String       doc    = string_lit("Query the texture height in pixels.");
    const ScriptMask   ret    = script_mask_num;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_texture_height);
  }
  {
    const String       name   = string_lit("texture_layers");
    const String       doc    = string_lit("Query the amount of texture layers.");
    const ScriptMask   ret    = script_mask_num;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_texture_layers);
  }
  {
    const String       name   = string_lit("texture_mips");
    const String       doc    = string_lit("Query or change the amount of mip levels.\nNote: Provide 0 to set the maximum amount of mips.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("mips"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_mips);
  }
  {
    const String       name   = string_lit("texture_mips_max");
    const String       doc    = string_lit("Query the maximum amount of mip levels.");
    const ScriptMask   ret    = script_mask_num;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_texture_mips_max);
  }
  {
    const String       name   = string_lit("texture_flip_y");
    const String       doc    = string_lit("Apply a y axis mirror.");
    const ScriptMask   ret    = script_mask_null;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_texture_flip_y);
  }
  {
    const String       name   = string_lit("texture_resize");
    const String       doc    = string_lit("Resize the current texture.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("width"), script_mask_num},
        {string_lit("height"), script_mask_num},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_resize);
  }
  {
    const String       name   = string_lit("texture_trans_mul");
    const String       doc    = string_lit("Multiply each pixel by the given color.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("color"), script_mask_color},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_trans_mul);
  }
  {
    const String       name   = string_lit("texture_trans_add");
    const String       doc    = string_lit("Add the given color to each pixel.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("color"), script_mask_color},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_trans_add);
  }
  {
    const String       name   = string_lit("texture_trans_sub");
    const String       doc    = string_lit("Subtract the given color from each pixel.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("color"), script_mask_color},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_trans_sub);
  }
  {
    const String       name   = string_lit("texture_trans_gray");
    const String       doc    = string_lit("Convert each pixel to gray-scale using the Rec709 luminance coefficients.");
    const ScriptMask   ret    = script_mask_null;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_texture_trans_gray);
  }
  {
    const String       name   = string_lit("texture_trans_shift");
    const String       doc    = string_lit("Shift the color of each pixel.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("hue"), script_mask_num},
        {string_lit("saturation"), script_mask_num | script_mask_null},
        {string_lit("value"), script_mask_num | script_mask_null},
        {string_lit("alpha"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_trans_shift);
  }
  {
    const String       name   = string_lit("texture_trans_replace");
    const String       doc    = string_lit("Replace a specific hue with another.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("oldHue"), script_mask_num},
        {string_lit("newHue"), script_mask_num},
        {string_lit("threshold"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_trans_replace);
  }
  // clang-format on

  asset_import_register(binder);

  script_binder_finalize(binder);
  g_assetScriptImportTextureBinder = binder;
}

bool asset_import_texture(
    const AssetImportEnvComp*     env,
    const String                  id,
    const Mem                     data,
    const u32                     width,
    const u32                     height,
    const u32                     channels,
    const AssetTextureType        type,
    const AssetImportTextureFlags importFlags,
    const AssetImportTextureFlip  importFlip,
    AssetTextureComp*             out) {

  Mem  outMem       = data;
  bool outMemOwning = false;
  bool success      = false;

  diag_assert(data.size == width * height * channels * import_texture_type_size(type));

  AssetImportTexture ctx = {
      .flags        = importFlags,
      .flip         = importFlip,
      .width        = width,
      .height       = height,
      .channels     = channels,
      .type         = type,
      .layers       = 1,
      .data         = data,
      .dataWidth    = width,
      .dataHeight   = height,
      .dataChannels = channels,
      .dataType     = type,
  };
  if (!asset_import_eval(env, g_assetScriptImportTextureBinder, id, &ctx)) {
    goto Ret;
  }

  // Apply resize.
  if (ctx.width != width || ctx.height != height || ctx.channels != channels || ctx.type != type) {
    const u32   dstPixelCount = ctx.width * ctx.height;
    const usize dstTypeSize   = import_texture_type_size(ctx.type);

    outMem = alloc_alloc(g_allocHeap, dstPixelCount * ctx.channels * dstTypeSize, dstTypeSize);
    outMemOwning = true;

    asset_texture_convert(
        data, width, height, channels, type, outMem, ctx.width, ctx.height, ctx.channels, ctx.type);
  }

  // Apply flip.
  if (ctx.flip & AssetImportTextureFlip_Y) {
    asset_texture_flip_y(outMem, ctx.width, ctx.height, channels, ctx.type);
  }

  AssetTextureFlags outFlags = 0;
  if (ctx.flags & AssetImportTextureFlags_Mips) {
    outFlags |= AssetTextureFlags_GenerateMips;
  }
  if (ctx.flags & AssetImportTextureFlags_Linear) {
    // Explicitly linear.
  } else if (ctx.channels >= 3 && ctx.type == AssetTextureType_u8) {
    outFlags |= AssetTextureFlags_Srgb;
  }
  if (ctx.flags & AssetImportTextureFlags_Lossless) {
    outFlags |= AssetTextureFlags_Lossless;
  }

  if (outFlags & AssetTextureFlags_Srgb && ctx.channels < 3) {
    goto Ret;
  }

  // Output texture.
  *out = asset_texture_create(
      outMem,
      ctx.width,
      ctx.height,
      ctx.channels,
      ctx.layers,
      1 /* mipsSrc */,
      ctx.mips,
      ctx.type,
      outFlags);

  success = true;

Ret:
  if (outMemOwning) {
    alloc_free(g_allocHeap, outMem);
  }
  return success;
}
