#include "core.h"
#include "core_array.h"
#include "core_deflate.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_zlib.h"

/**
 * ZLIB (RFC 1950) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1950
 */

#define VOLO_ZLIB_VALIDATE_CHECKSUM 0

typedef enum {
  ZlibMethod_Deflate = 8,
} ZlibMethod;

static const String g_errorStrs[] = {
    string_static("None"),
    string_static("Truncated"),
    string_static("UnsupportedMethod"),
    string_static("DeflateError"),
    string_static("ChecksumError"),
};

ASSERT(array_elems(g_errorStrs) == ZlibError_Count, "Incorrect number of ZlibError strings");

String zlib_error_str(const ZlibError err) {
  diag_assert(err < ZlibError_Count);
  return g_errorStrs[err];
}

String zlib_decode(String input, DynString* out, ZlibError* err) {
  MAYBE_UNUSED const usize outOffset = out->size;
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

#if VOLO_ZLIB_VALIDATE_CHECKSUM
  // Verify the checksum.
  const Mem outMem = mem_consume(dynstring_view(out), outOffset);
  if (UNLIKELY(bits_adler_32(1, outMem) != checksum)) {
    *err = ZlibError_ChecksumError;
    return input;
  }
#endif

  *err = ZlibError_None;
  return input;
}
