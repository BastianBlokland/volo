#include "core_format.h"

#include "cli_validate.h"

bool cli_validate_i64(const String input) {
  const u8     base = 10;
  const String rem  = format_read_i64(input, null, base);
  return string_is_empty(rem);
}

bool cli_validate_u64(const String input) {
  const u8     base = 10;
  const String rem  = format_read_u64(input, null, base);
  return string_is_empty(rem);
}

bool cli_validate_f64(const String input) {
  const String rem = format_read_f64(input, null);
  return string_is_empty(rem);
}
