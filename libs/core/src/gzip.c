#include "core_annotation.h"
#include "core_gzip.h"
#include "core_time.h"

/**
 * GZIP (RFC 1952) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1952
 */

typedef enum {
  GzipFlags_Text    = 1 << 0,
  GzipFlags_HCrc    = 1 << 1,
  GzipFlags_Extra   = 1 << 2,
  GzipFlags_Name    = 1 << 3,
  GzipFlags_Comment = 1 << 4,
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
  String     input;
  DynString* out;
} UnzipCtx;

static void unzip_read_header(UnzipCtx* ctx, GzipHeader* out, GzipError* err) {
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

static void unzip(UnzipCtx* ctx, GzipError* err) {
  GzipHeader header;
  unzip_read_header(ctx, &header, err);
  if (UNLIKELY(*err)) {
    return;
  }
  if (UNLIKELY(header.method != GzipMethod_Deflate)) {
    *err = GzipError_UnsupportedMethod;
    return;
  }
}

String gzip_decode(const String input, DynString* out, GzipError* err) {
  UnzipCtx ctx = {
      .input = input,
      .out   = out,
  };
  *err = GzipError_None;
  unzip(&ctx, err);
  return ctx.input;
}
