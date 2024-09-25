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

INLINE_HINT static void inflate_mem_consume_inplace(Mem* mem, const usize amount) {
  mem->ptr = bits_ptr_offset(mem->ptr, amount);
  mem->size -= amount;
}

static u32 inflate_read_int(InflateCtx* ctx, const u32 bits, DeflateError* err) {
  diag_assert(bits <= 32);
  u32 res = 0;
  for (u32 i = 0; i != bits; ++i) {
    if (ctx->inputBitIndex == 8) {
      inflate_mem_consume_inplace(&ctx->input, 1);
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
    inflate_mem_consume_inplace(&ctx->input, 1);
    ctx->inputBitIndex = 0;
  }
}

static bool inflate_block(InflateCtx* ctx, DeflateError* err) {
  (void)ctx;
  *err = DeflateError_Unknown;
  return false;
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
