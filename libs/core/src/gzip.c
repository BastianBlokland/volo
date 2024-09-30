#include "core_annotation.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_deflate.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_gzip.h"
#include "core_time.h"

/**
 * GZIP (RFC 1952) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1952
 */

typedef enum {
  GzipFlags_Text      = 1 << 0,
  GzipFlags_HeaderCrc = 1 << 1,
  GzipFlags_Extra     = 1 << 2,
  GzipFlags_Name      = 1 << 3,
  GzipFlags_Comment   = 1 << 4,
} GzipFlags;

typedef enum {
  GzipMethod_Deflate = 0,
  GzipMethod_Other   = 1,
} GzipMethod;

typedef struct {
  GzipMethod method;
  GzipFlags  flags;
  TimeReal   modTime;
} GzipHeader;

typedef struct {
  String     input, inputFull;
  DynString* out;

  GzipHeader header;
  String     name, comment;
} UnzipCtx;

static const String g_errorStrs[] = {
    string_static("None"),
    string_static("Truncated"),
    string_static("Malformed"),
    string_static("UnsupportedMethod"),
    string_static("DeflateError"),
    string_static("ChecksumError"),
    string_static("Unknown"),
};

ASSERT(array_elems(g_errorStrs) == GzipError_Count, "Incorrect number of GzipError strings");

static void gzip_read_header(UnzipCtx* ctx, GzipHeader* out, GzipError* err) {
  if (UNLIKELY(ctx->input.size < 10)) {
    *err = GzipError_Truncated;
    return;
  }

  // Read id bytes.
  u8 idBytes[2];
  ctx->input = mem_consume_u8(ctx->input, &idBytes[0]);
  ctx->input = mem_consume_u8(ctx->input, &idBytes[1]);
  if (UNLIKELY(idBytes[0] != 0x1F || idBytes[1] != 0x8B)) {
    *err = GzipError_Malformed;
  }

  // Read compression method.
  u8 method;
  ctx->input = mem_consume_u8(ctx->input, &method);
  switch (method) {
  case 8:
    out->method = GzipMethod_Deflate;
    break;
  default:
    if (UNLIKELY(method < 8)) {
      *err = GzipError_Malformed;
      return;
    }
    out->method = GzipMethod_Other;
  }

  // Read flags.
  u8 flagBits;
  ctx->input = mem_consume_u8(ctx->input, &flagBits);
  if (UNLIKELY(flagBits & 0xE0)) {
    *err = GzipError_Malformed;
    return;
  }
  out->flags = (GzipFlags)flagBits;

  // Read mod-time.
  u32 modTimeEpochSeconds;
  ctx->input   = mem_consume_le_u32(ctx->input, &modTimeEpochSeconds);
  out->modTime = time_real_offset(time_real_epoch, time_seconds(modTimeEpochSeconds));

  // Skip over extra flags and OS enum.
  ctx->input = mem_consume(ctx->input, 2);
}

static void gzip_read_extra(UnzipCtx* ctx, GzipError* err) {
  if (UNLIKELY(ctx->input.size < 2)) {
    *err = GzipError_Truncated;
    return;
  }
  u16 extraLen;
  ctx->input = mem_consume_le_u16(ctx->input, &extraLen);
  if (UNLIKELY(ctx->input.size < extraLen)) {
    *err = GzipError_Truncated;
    return;
  }
  // Skip over extra data.
  ctx->input = mem_consume(ctx->input, extraLen);
}

static String gzip_read_string(UnzipCtx* ctx, GzipError* err) {
  usize inputIdx = 0;
  for (;;) {
    if (UNLIKELY(inputIdx >= ctx->input.size)) {
      *err = GzipError_Truncated;
      return string_empty;
    }
    const u8 ch = *string_at(ctx->input, inputIdx++);
    if (ch == '\0') {
      break; // Reached null-terminator.
    }
  }
  const String result = mem_slice(ctx->input, 0, inputIdx - 1 /* remove the null-terminator */);
  ctx->input          = mem_consume(ctx->input, inputIdx);
  return result;
}

static u16 gzip_read_header_crc(UnzipCtx* ctx, GzipError* err) {
  if (UNLIKELY(ctx->input.size < 2)) {
    *err = GzipError_Truncated;
    return 0;
  }
  u16 crc;
  ctx->input = mem_consume_le_u16(ctx->input, &crc);
  return crc;
}

static void gzip_read_data(UnzipCtx* ctx, GzipError* err) {
  const usize outOffset = ctx->out->size;

  DeflateError deflateErr;
  ctx->input = deflate_decode(ctx->input, ctx->out, &deflateErr);
  if (UNLIKELY(deflateErr)) {
    *err = GzipError_DeflateError;
    return;
  }

  if (UNLIKELY(ctx->input.size < 8)) {
    *err = GzipError_Truncated;
    return;
  }
  u32 crc, length;
  ctx->input = mem_consume_le_u32(ctx->input, &crc);
  ctx->input = mem_consume_le_u32(ctx->input, &length);

  if (UNLIKELY(ctx->out->size - outOffset != length)) {
    *err = GzipError_Malformed;
    return;
  }

  const Mem outMem = mem_consume(dynstring_view(ctx->out), outOffset);
  if (UNLIKELY(bits_crc_32(0, outMem) != crc)) {
    *err = GzipError_ChecksumError;
    return;
  }
}

static void gzip_read(UnzipCtx* ctx, GzipError* err) {
  gzip_read_header(ctx, &ctx->header, err);
  if (UNLIKELY(*err)) {
    return;
  }
  if (UNLIKELY(ctx->header.method != GzipMethod_Deflate)) {
    *err = GzipError_UnsupportedMethod;
    return;
  }
  if (ctx->header.flags & GzipFlags_Extra) {
    gzip_read_extra(ctx, err);
    if (UNLIKELY(*err)) {
      return;
    }
  }
  if (ctx->header.flags & GzipFlags_Name) {
    ctx->name = gzip_read_string(ctx, err);
    if (UNLIKELY(*err)) {
      return;
    }
  }
  if (ctx->header.flags & GzipFlags_Comment) {
    ctx->comment = gzip_read_string(ctx, err);
    if (UNLIKELY(*err)) {
      return;
    }
  }
  if (ctx->header.flags & GzipFlags_HeaderCrc) {
    const Mem headerMem = mem_slice(ctx->inputFull, 0, ctx->inputFull.size - ctx->input.size);
    const u16 headerCrc = gzip_read_header_crc(ctx, err);
    if (UNLIKELY(*err)) {
      return;
    }
    if (UNLIKELY((bits_crc_32(0, headerMem) & 0x0000FFFF) != headerCrc)) {
      *err = GzipError_ChecksumError;
      return;
    }
  }
  gzip_read_data(ctx, err);
}

String gzip_error_str(const GzipError err) {
  diag_assert(err < GzipError_Count);
  return g_errorStrs[err];
}

String gzip_decode(const String input, GzipMeta* outMeta, DynString* out, GzipError* err) {
  UnzipCtx ctx = {
      .input     = input,
      .inputFull = input,
      .out       = out,
  };
  *err = GzipError_None;
  gzip_read(&ctx, err);
  if (outMeta) {
    *outMeta = (GzipMeta){
        .name    = ctx.name,
        .comment = ctx.comment,
        .modTime = ctx.header.modTime,
    };
  }
  return ctx.input;
}
