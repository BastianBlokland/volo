#include "core_annotation.h"
#include "core_bits.h"
#include "core_deflate.h"
#include "core_dynstring.h"
#include "core_zlib.h"

/**
 * ZLIB (RFC 1950) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1950
 */

typedef enum {
  ZlibMethod_Deflate = 8,
} ZlibMethod;

String zlib_decode(String input, DynString* out, ZlibError* err) {
  const usize outOffset = out->size;
  if (UNLIKELY(input.size < 2)) {
    *err = ZlibError_Truncated;
    return input;
  }
  u8 cmf, flg;
  input = mem_consume_u8(input, &cmf);
  input = mem_consume_u8(input, &flg);

  // Verify header checksum.
  if ((256 * cmf + flg) % 31) {
    *err = ZlibError_ChecksumError;
    return input;
  }

  // Check the used compression method.
  const u8 method = cmf & 0x0F;
  if (UNLIKELY(method != ZlibMethod_Deflate)) {
    *err = ZlibError_UnsupportedMethod;
    return input;
  }

  // Verify that no preset-dictionary was used.
  const bool usesPresetDictionary = (flg & 0x20) != 0;
  if (UNLIKELY(usesPresetDictionary)) {
    *err = ZlibError_UnsupportedMethod;
    return input;
  }

  // Decompress the data.
  DeflateError deflateErr;
  input = deflate_decode(input, out, &deflateErr);
  if (UNLIKELY(deflateErr)) {
    *err = ZlibError_DeflateError;
    return input;
  }

  // Read the checksum.
  if (UNLIKELY(input.size < 4)) {
    *err = ZlibError_Truncated;
    return input;
  }
  u32 checksum;
  input = mem_consume_be_u32(input, &checksum);

  // Verify the checksum.
  const Mem outMem = mem_consume(dynstring_view(out), outOffset);
  if (UNLIKELY(bits_adler_32(1, outMem) != checksum)) {
    *err = ZlibError_ChecksumError;
    return input;
  }

  *err = ZlibError_None;
  return input;
}
