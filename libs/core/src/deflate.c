#include "core_deflate.h"

/**
 * DEFLATE (RFC 1951) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1951
 */

typedef struct {
  String     input;
  DynString* out;
} InflateCtx;

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
