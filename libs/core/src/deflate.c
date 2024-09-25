#include "core_annotation.h"
#include "core_bits.h"
#include "core_deflate.h"
#include "core_diag.h"

/**
 * DEFLATE (RFC 1951) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1951
 */

typedef struct {
  String     input;
  u32        inputBitIndex;
  DynString* out;
} InflateCtx;

static void inflate_consume(InflateCtx* ctx, const usize amount) {
  ctx->input.ptr = bits_ptr_offset(ctx->input.ptr, amount);
  ctx->input.size -= amount;
}

static u32 inflate_read_unaligned(InflateCtx* ctx, const u32 bits, DeflateError* err) {
  diag_assert(bits <= 32);
  u32 res = 0;
  for (u32 i = 0; i != bits; ++i) {
    if (ctx->inputBitIndex == 8) {
      inflate_consume(ctx, 1);
      ctx->inputBitIndex = 0;
    }
    if (UNLIKELY(!ctx->input.size)) {
      *err = DeflateError_Truncated;
      return res;
    }

    // Extract one bit from the input.
    const u32 bit = (*mem_begin(ctx->input) >> ctx->inputBitIndex++) & 1;

    // Append it to the result.
    res |= bit << i;
  }
  return res;
}

static void inflate_read_align(InflateCtx* ctx) {
  if (ctx->inputBitIndex) {
    diag_assert(ctx->input.size);
    inflate_consume(ctx, 1);
    ctx->inputBitIndex = 0;
  }
}

static u16 inflate_read_u16(InflateCtx* ctx, DeflateError* err) {
  inflate_read_align(ctx);
  if (UNLIKELY(ctx->input.size < sizeof(u16))) {
    *err = DeflateError_Truncated;
    return 0;
  }
  u8*       data = mem_begin(ctx->input);
  const u16 val  = (u16)data[0] | (u16)data[1] << 8;
  inflate_consume(ctx, 2);
  return val;
}

static void inflate_block_uncompressed(InflateCtx* ctx, DeflateError* err) {
  const u16 len  = inflate_read_u16(ctx, err);
  const u16 nlen = inflate_read_u16(ctx, err);
  if (UNLIKELY(*err)) {
    return;
  }
  if (UNLIKELY((u16)~len != nlen)) {
    *err = DeflateError_Malformed;
    return;
  }
  if (UNLIKELY(ctx->input.size < len)) {
    *err = DeflateError_Truncated;
    return;
  }
  dynstring_append(ctx->out, mem_slice(ctx->input, 0, len));
  inflate_consume(ctx, len);
}

static bool inflate_block(InflateCtx* ctx, DeflateError* err) {
  const bool finalBlock = inflate_read_unaligned(ctx, 1, err);
  const u32  type       = inflate_read_unaligned(ctx, 2, err);

  if (UNLIKELY(*err)) {
    return false;
  }

  switch (type) {
  case 0: /* no compression */
    inflate_block_uncompressed(ctx, err);
    break;
  case 1: /* compressed with fixed Huffman codes */
    break;
  case 2: /* compressed with dynamic Huffman codes */
    break;
  case 3: /* reserved */
    *err = DeflateError_Malformed;
    return false;
  default:
    UNREACHABLE
  }

  return !finalBlock;
}

String deflate_decode(const String input, DynString* out, DeflateError* err) {
  InflateCtx ctx = {
      .input = input,
      .out   = out,
  };
  *err = DeflateError_None;
  while (inflate_block(&ctx, err) && *err == DeflateError_None)
    ;
  return ctx.input;
}
