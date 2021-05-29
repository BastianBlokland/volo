#include "core_diag.h"
#include "core_format.h"

void format_write_u64(DynString* str, u64 val, const FormatIntOptions* opts) {
  diag_assert(opts->base > 1 && opts->base <= 16);

  Mem buffer = mem_stack(64);
  u8* ptr    = mem_end(buffer);

  const char* chars = "0123456789ABCDEF";
  do {
    *--ptr = chars[val % opts->base];
    val /= opts->base;
  } while (val);

  const u8 numDigits = mem_end(buffer) - ptr;
  dynstring_append(str, mem_create(ptr, numDigits));
}

void format_write_i64(DynString* str, const i64 val, const FormatIntOptions* opts) {
  if (val < 0) {
    dynstring_append_char(str, '-');
    return format_write_u64(str, -val, opts);
  }
  return format_write_u64(str, val, opts);
}
