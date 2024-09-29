#include "core_gzip.h"

/**
 * GZIP (RFC 1952) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1952
 */

String gzip_decode(const String input, DynString* out, GzipError* err) {
  (void)input;
  (void)out;
  (void)err;
  return input;
}
