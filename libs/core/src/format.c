#include "core_diag.h"
#include "core_float.h"
#include "core_format.h"

void format_write_u64(DynString* str, u64 val, const FormatOptsInt* opts) {
  diag_assert(opts->base > 1 && opts->base <= 16);

  Mem buffer = mem_stack(64);
  u8* ptr    = mem_end(buffer);

  const char* chars         = "0123456789ABCDEF";
  u8          digitsWritten = 0;
  do {
    *--ptr = chars[val % opts->base];
    val /= opts->base;
  } while (++digitsWritten < opts->minDigits || val);

  const u8 numDigits = mem_end(buffer) - ptr;
  dynstring_append(str, mem_create(ptr, numDigits));
}

void format_write_i64(DynString* str, i64 val, const FormatOptsInt* opts) {
  if (val < 0) {
    dynstring_append_char(str, '-');
    val = -val;
  }
  return format_write_u64(str, val, opts);
}

void format_write_f64(DynString* str, f64 val, const FormatOptsFloat* opts) {
  if (float_isnan(val)) {
    dynstring_append(str, string_lit("nan"));
    return;
  }

  if (val < 0.0) {
    dynstring_append_char(str, '-');
    val = -val;
  }

  if (float_isinf(val)) {
    dynstring_append(str, string_lit("inf"));
    return;
  }

  // TODO: Implement :)

  (void)str;
  (void)val;
  (void)opts;
}
