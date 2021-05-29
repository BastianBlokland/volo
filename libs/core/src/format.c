#include "core_diag.h"
#include "core_format.h"

void format_write_u64(DynString* str, const u64 val, const FormatIntOptions* opts) {
  diag_assert(opts->base > 1 && opts->base <= 16);

  const char* chars = "0123456789ABCDEF";
  if (val < opts->base) {
    dynstring_append_char(str, chars[val]);
    return;
  }
  format_write_u64(str, val / opts->base, opts);
  dynstring_append_char(str, chars[val % opts->base]);
}

void format_write_i64(DynString* str, const i64 val, const FormatIntOptions* opts) {
  if (val < 0) {
    dynstring_append_char(str, '-');
    return format_write_u64(str, -val, opts);
  }
  return format_write_u64(str, val, opts);
}
