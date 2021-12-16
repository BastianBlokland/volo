#include "cli_parse.h"
#include "cli_read.h"
#include "core_format.h"

String cli_read_string(const CliInvocation* invoc, const CliId id, const String defaultVal) {
  CliParseValues values = cli_parse_values(invoc, id);
  return values.count ? *values.values : defaultVal;
}

i64 cli_read_i64(const CliInvocation* invoc, const CliId id, const i64 defaultVal) {
  CliParseValues values = cli_parse_values(invoc, id);
  if (!values.count) {
    return defaultVal;
  }
  i64      result;
  const u8 base = 10;
  format_read_i64(values.values[0], &result, base);
  return result;
}

u64 cli_read_u64(const CliInvocation* invoc, const CliId id, const u64 defaultVal) {
  CliParseValues values = cli_parse_values(invoc, id);
  if (!values.count) {
    return defaultVal;
  }
  u64      result;
  const u8 base = 10;
  format_read_u64(values.values[0], &result, base);
  return result;
}

f64 cli_read_f64(const CliInvocation* invoc, const CliId id, const f64 defaultVal) {
  CliParseValues values = cli_parse_values(invoc, id);
  if (!values.count) {
    return defaultVal;
  }
  f64 result;
  format_read_f64(values.values[0], &result);
  return result;
}

usize cli_read_choice(
    const CliInvocation* invoc,
    const CliId          id,
    const String*        choiceStrs,
    const usize          choicesCount,
    const usize          defaultVal) {
  CliParseValues values = cli_parse_values(invoc, id);
  if (!values.count) {
    return defaultVal;
  }
  for (usize i = 0; i != choicesCount; ++i) {
    if (string_eq(*values.values, choiceStrs[i])) {
      return i;
    }
  }
  return defaultVal;
}
