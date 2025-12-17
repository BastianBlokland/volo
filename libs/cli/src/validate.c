#include "cli/validate.h"
#include "core/file.h"
#include "core/format.h"

bool cli_validate_i64(const String input) {
  const u8     base = 10;
  const String rem  = format_read_i64(input, null, base);
  return string_is_empty(rem);
}

bool cli_validate_u8(const String input) {
  const u8     base = 10;
  u64          value;
  const String rem = format_read_u64(input, &value, base);
  return string_is_empty(rem) && value <= u8_max;
}

bool cli_validate_u16(const String input) {
  const u8     base = 10;
  u64          value;
  const String rem = format_read_u64(input, &value, base);
  return string_is_empty(rem) && value <= u16_max;
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

bool cli_validate_file(const String input) {
  return file_stat_path_sync(input).type != FileType_None;
}

bool cli_validate_file_regular(const String input) {
  return file_stat_path_sync(input).type == FileType_Regular;
}

bool cli_validate_file_directory(const String input) {
  return file_stat_path_sync(input).type == FileType_Directory;
}
