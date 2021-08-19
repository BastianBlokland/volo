#include "core_format.h"

#include "cli_parse.h"
#include "cli_read.h"

i64 cli_read_i64(CliInvocation* invoc, const CliId id, i64 defaultVal) {
  CliParseValues values = cli_parse_values(invoc, id);
  if (!values.count) {
    return defaultVal;
  }
  i64      result;
  const u8 base = 10;
  format_read_i64(values.head[0], &result, base);
  return result;
}

u64 cli_read_u64(CliInvocation* invoc, const CliId id, u64 defaultVal) {
  CliParseValues values = cli_parse_values(invoc, id);
  if (!values.count) {
    return defaultVal;
  }
  u64      result;
  const u8 base = 10;
  format_read_u64(values.head[0], &result, base);
  return result;
}

f64 cli_read_f64(CliInvocation* invoc, const CliId id, f64 defaultVal) {
  CliParseValues values = cli_parse_values(invoc, id);
  if (!values.count) {
    return defaultVal;
  }
  f64 result;
  format_read_f64(values.head[0], &result);
  return result;
}
