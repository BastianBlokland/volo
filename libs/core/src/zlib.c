#include "core_dynstring.h"
#include "core_zlib.h"

/**
 * ZLIB (RFC 1950) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1950
 */

String zlib_decode(const String input, DynString* out, ZlibError* err) {
  (void)out;
  (void)err;
  return input;
}
