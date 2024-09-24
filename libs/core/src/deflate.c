#include "core_deflate.h"

/**
 * DEFLATE (RFC 1951) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1951
 */

String deflate_decode(const String input, DynString* out, DeflateError* err) {
  (void)out;
  (void)err;
  return input;
}
