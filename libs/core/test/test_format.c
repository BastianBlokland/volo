#include "core_diag.h"
#include "core_float.h"
#include "core_format.h"

static void test_format_write_arg(const FormatArg* arg, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_arg(&string, arg);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void
test_format_write_formatted(String format, const FormatArg* args, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_formatted(&string, format, args);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_u64(const u64 val, const FormatOptsInt opts, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_u64(&string, val, &opts);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_i64(const i64 val, const FormatOptsInt opts, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_i64(&string, val, &opts);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void
test_format_write_f64(const f64 val, const FormatOptsFloat opts, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_f64(&string, val, &opts);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_bool(const bool val, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_bool(&string, val);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_bitset(const BitSet val, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_bitset(&string, val);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_mem() {
  DynString string = dynstring_create_over(mem_stack(128));

  u64 testData = 0x8BADF00DDEADBEEF;
  Mem testMem  = mem_create(&testData, sizeof(testData));

  format_write_mem(&string, testMem);
  diag_assert(string_eq(dynstring_view(&string), string_lit("8BADF00DDEADBEEF")));

  dynstring_destroy(&string);
}

static void test_format_write_time_duration_pretty(const TimeDuration dur, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_time_duration_pretty(&string, dur);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_time_iso8601(const TimeReal time, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_time_iso8601(&string, time, &format_opts_time());
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_size_pretty(const usize val, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_size_pretty(&string, val);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_text(const String val, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  format_write_text(&string, val, &format_opts_text(.flags = FormatTextFlags_EscapeNonPrintAscii));
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_read_whitespace(
    const String val, const String expected, const String expectedRemaining) {
  String       out;
  const String rem = format_read_whitespace(val, &out);
  diag_assert(string_eq(out, expected));
  diag_assert(string_eq(rem, expectedRemaining));
}

static void test_format_read_u64(
    const String val, const u8 base, const u64 expected, const String expectedRemaining) {
  u64          out;
  const String rem = format_read_u64(val, &out, base);
  diag_assert(out == expected);
  diag_assert(string_eq(rem, expectedRemaining));
}

static void test_format_read_i64(
    const String val, const u8 base, const i64 expected, const String expectedRemaining) {
  i64          out;
  const String rem = format_read_i64(val, &out, base);

  diag_assert(out == expected);
  diag_assert(string_eq(rem, expectedRemaining));
}

void test_format() {
  test_format_write_arg(&fmt_int(42), string_lit("42"));
  test_format_write_arg(&fmt_int(-42), string_lit("-42"));
  test_format_write_arg(&fmt_int(42, .base = 16), string_lit("2A"));
  test_format_write_arg(&fmt_float(42.42), string_lit("42.42"));
  test_format_write_arg(&fmt_bool(true), string_lit("true"));
  test_format_write_arg(&fmt_mem(string_lit("Hello")), string_lit("6F6C6C6548"));
  test_format_write_arg(&fmt_duration(time_minute), string_lit("1m"));
  test_format_write_arg(&fmt_size(usize_mebibyte), string_lit("1MiB"));
  test_format_write_arg(&fmt_text_lit("Hello World"), string_lit("Hello World"));
  test_format_write_arg(&fmt_path(string_lit("c:\\hello")), string_lit("C:/hello"));

  test_format_write_formatted(
      string_lit("Value {}"), fmt_args(fmt_int(42)), string_lit("Value 42"));
  test_format_write_formatted(string_lit("hello world"), fmt_args(), string_lit("hello world"));
  test_format_write_formatted(
      string_lit("{} hello world {  }-{ \t }"),
      fmt_args(fmt_bool(false), fmt_int(42), fmt_bool(true)),
      string_lit("false hello world 42-true"));
  test_format_write_formatted(
      string_lit("{>4}|{<4}|"), fmt_args(fmt_int(1), fmt_int(20)), string_lit("   1|20  |"));
  test_format_write_formatted(
      string_lit("{ >4 }|{ >4}|"), fmt_args(fmt_int(1), fmt_int(20)), string_lit("   1|  20|"));

  test_format_write_u64(0, format_opts_int(), string_lit("0"));
  test_format_write_u64(0, format_opts_int(.minDigits = 4), string_lit("0000"));
  test_format_write_u64(1, format_opts_int(), string_lit("1"));
  test_format_write_u64(42, format_opts_int(), string_lit("42"));
  test_format_write_u64(42, format_opts_int(.minDigits = 2), string_lit("42"));
  test_format_write_u64(42, format_opts_int(.minDigits = 4), string_lit("0042"));
  test_format_write_u64(1337, format_opts_int(), string_lit("1337"));
  test_format_write_u64(u64_max, format_opts_int(), string_lit("18446744073709551615"));

  test_format_write_u64(0, format_opts_int(.base = 2), string_lit("0"));
  test_format_write_u64(1, format_opts_int(.base = 2), string_lit("1"));
  test_format_write_u64(2, format_opts_int(.base = 2), string_lit("10"));
  test_format_write_u64(0b010110110, format_opts_int(.base = 2), string_lit("10110110"));
  test_format_write_u64(255, format_opts_int(.base = 2), string_lit("11111111"));

  test_format_write_u64(0x0, format_opts_int(.base = 16), string_lit("0"));
  test_format_write_u64(0x9, format_opts_int(.base = 16), string_lit("9"));
  test_format_write_u64(0xF, format_opts_int(.base = 16), string_lit("F"));
  test_format_write_u64(0xDEADBEEF, format_opts_int(.base = 16), string_lit("DEADBEEF"));
  test_format_write_u64(u64_max, format_opts_int(.base = 16), string_lit("FFFFFFFFFFFFFFFF"));

  test_format_write_i64(0, format_opts_int(), string_lit("0"));
  test_format_write_i64(-0, format_opts_int(), string_lit("0"));
  test_format_write_i64(1, format_opts_int(), string_lit("1"));
  test_format_write_i64(-1, format_opts_int(), string_lit("-1"));
  test_format_write_i64(-42, format_opts_int(), string_lit("-42"));
  test_format_write_i64(1337, format_opts_int(), string_lit("1337"));
  test_format_write_i64(i64_min, format_opts_int(), string_lit("-9223372036854775808"));
  test_format_write_i64(i64_max, format_opts_int(), string_lit("9223372036854775807"));

  test_format_write_f64(f64_nan, format_opts_float(), string_lit("nan"));
  test_format_write_f64(f64_inf, format_opts_float(), string_lit("inf"));
  test_format_write_f64(-f64_inf, format_opts_float(), string_lit("-inf"));
  test_format_write_f64(0, format_opts_float(), string_lit("0"));
  test_format_write_f64(42, format_opts_float(), string_lit("42"));
  test_format_write_f64(42.0042, format_opts_float(), string_lit("42.0042"));
  test_format_write_f64(42.42, format_opts_float(), string_lit("42.42"));
  test_format_write_f64(
      1337.13371337, format_opts_float(.maxDecDigits = 8), string_lit("1337.13371337"));
  test_format_write_f64(
      1337.133713371337, format_opts_float(.maxDecDigits = 12), string_lit("1337.133713371337"));
  test_format_write_f64(1337133713371337, format_opts_float(), string_lit("1.3371337e15"));
  test_format_write_f64(
      1337133713371337, format_opts_float(.expThresholdPos = 1e16), string_lit("1337133713371337"));
  test_format_write_f64(.0000000001, format_opts_float(.maxDecDigits = 10), string_lit("1e-10"));
  test_format_write_f64(
      .0000000001,
      format_opts_float(.maxDecDigits = 10, .expThresholdNeg = 1e-11),
      string_lit("0.0000000001"));
  test_format_write_f64(10, format_opts_float(.expThresholdPos = 1), string_lit("1e1"));

  test_format_write_f64(0, format_opts_float(.minDecDigits = 2), string_lit("0.00"));
  test_format_write_f64(42, format_opts_float(.minDecDigits = 1), string_lit("42.0"));
  test_format_write_f64(42.0042, format_opts_float(.minDecDigits = 5), string_lit("42.00420"));

  test_format_write_f64(42.0042, format_opts_float(.maxDecDigits = 2), string_lit("42"));
  test_format_write_f64(42.005, format_opts_float(.maxDecDigits = 2), string_lit("42.01"));
  test_format_write_f64(42.005, format_opts_float(.maxDecDigits = 0), string_lit("42"));

  test_format_write_f64(f64_min, format_opts_float(), string_lit("-1.7976931e308"));
  test_format_write_f64(f64_max, format_opts_float(), string_lit("1.7976931e308"));

  test_format_write_f64(1e255, format_opts_float(), string_lit("1e255"));
  test_format_write_f64(1e-255, format_opts_float(), string_lit("1e-255"));

  test_format_write_f64(f64_epsilon, format_opts_float(), string_lit("4.9406565e-324"));

  test_format_write_bool(true, string_lit("true"));
  test_format_write_bool(false, string_lit("false"));

  test_format_write_bitset(bitset_from_var((u8){0}), string_lit("00000000"));
  test_format_write_bitset(bitset_from_var((u8){0b01011101}), string_lit("01011101"));
  test_format_write_bitset(
      bitset_from_var((u16){0b0101110101011101}), string_lit("0101110101011101"));

  test_format_write_mem();

  test_format_write_time_duration_pretty(time_nanosecond, string_lit("1ns"));
  test_format_write_time_duration_pretty(-time_nanosecond, string_lit("-1ns"));
  test_format_write_time_duration_pretty(time_nanoseconds(42), string_lit("42ns"));
  test_format_write_time_duration_pretty(time_microsecond, string_lit("1us"));
  test_format_write_time_duration_pretty(time_microseconds(42), string_lit("42us"));
  test_format_write_time_duration_pretty(time_millisecond, string_lit("1ms"));
  test_format_write_time_duration_pretty(time_milliseconds(42), string_lit("42ms"));
  test_format_write_time_duration_pretty(time_second, string_lit("1s"));
  test_format_write_time_duration_pretty(time_seconds(42), string_lit("42s"));
  test_format_write_time_duration_pretty(time_minute, string_lit("1m"));
  test_format_write_time_duration_pretty(time_minutes(42), string_lit("42m"));
  test_format_write_time_duration_pretty(time_hour, string_lit("1h"));
  test_format_write_time_duration_pretty(time_hours(13), string_lit("13h"));
  test_format_write_time_duration_pretty(time_day, string_lit("1d"));
  test_format_write_time_duration_pretty(time_days(42), string_lit("42d"));
  test_format_write_time_duration_pretty(-time_days(42), string_lit("-42d"));
  test_format_write_time_duration_pretty(time_days(-42), string_lit("-42d"));
  test_format_write_time_duration_pretty(
      time_millisecond + time_microseconds(300), string_lit("1.3ms"));

  test_format_write_time_iso8601(time_real_epoch, string_lit("1970-01-01T00:00:00.000Z"));
  test_format_write_time_iso8601(
      time_real_offset(time_real_epoch, time_days(13)), string_lit("1970-01-14T00:00:00.000Z"));
  test_format_write_time_iso8601(
      time_real_offset(time_real_epoch, time_hours(13) + time_milliseconds(42)),
      string_lit("1970-01-01T13:00:00.042Z"));
  test_format_write_time_iso8601(
      time_real_offset(time_real_epoch, time_days(40) + time_hours(13) + time_milliseconds(42)),
      string_lit("1970-02-10T13:00:00.042Z"));

  test_format_write_size_pretty(42, string_lit("42B"));
  test_format_write_size_pretty(42 * usize_kibibyte, string_lit("42KiB"));
  test_format_write_size_pretty(42 * usize_mebibyte, string_lit("42MiB"));
  test_format_write_size_pretty(3 * usize_gibibyte, string_lit("3GiB"));
  if (sizeof(usize) == 8) { // 64 bit only sizes.
    test_format_write_size_pretty(42 * usize_gibibyte, string_lit("42GiB"));
    test_format_write_size_pretty(42 * usize_tebibyte, string_lit("42TiB"));
    test_format_write_size_pretty(42 * usize_pebibyte, string_lit("42PiB"));
    test_format_write_size_pretty(
        42 * usize_mebibyte + 200 * usize_kibibyte, string_lit("42.2MiB"));
    test_format_write_size_pretty(2048 * usize_pebibyte, string_lit("2048PiB"));
  }

  test_format_write_text(string_lit(""), string_lit(""));
  test_format_write_text(string_lit("\fHello\nWorld\\b"), string_lit("\\fHello\\nWorld\\b"));
  test_format_write_text(string_lit("Hello\0World"), string_lit("Hello\\0World"));
  test_format_write_text(
      string_lit("\xFFHello\xFBWorld\xFA"), string_lit("\\FFHello\\FBWorld\\FA"));

  test_format_read_whitespace(string_empty, string_empty, string_empty);
  test_format_read_whitespace(string_lit(" \t \n"), string_lit(" \t \n"), string_empty);
  test_format_read_whitespace(string_lit(" \t \nHello"), string_lit(" \t \n"), string_lit("Hello"));

  test_format_read_u64(string_empty, 10, 0, string_empty);
  test_format_read_u64(string_lit("1"), 10, 1, string_empty);
  test_format_read_u64(string_lit("1337"), 10, 1337, string_empty);
  test_format_read_u64(
      string_lit("18446744073709551615"), 10, 18446744073709551615ull, string_empty);
  test_format_read_u64(string_lit("1337-hello"), 10, 1337, string_lit("-hello"));
  test_format_read_u64(string_lit("42abc"), 10, 42, string_lit("abc"));
  test_format_read_u64(string_lit("Hello"), 10, 0, string_lit("Hello"));
  test_format_read_u64(string_lit("abcdef"), 16, 0xABCDEF, string_empty);
  test_format_read_u64(string_lit("123abcdef"), 16, 0x123ABCDEF, string_empty);
  test_format_read_u64(string_lit("123abcdef-hello"), 16, 0x123ABCDEF, string_lit("-hello"));

  test_format_read_i64(string_empty, 10, 0, string_empty);
  test_format_read_i64(string_lit("-42"), 10, -42, string_empty);
  test_format_read_i64(string_lit("+42"), 10, 42, string_empty);
  test_format_read_i64(string_lit("42"), 10, 42, string_empty);
  test_format_read_i64(string_lit("9223372036854775807"), 10, 9223372036854775807ll, string_empty);
  test_format_read_i64(string_lit("+9223372036854775807"), 10, 9223372036854775807ll, string_empty);
  test_format_read_i64(
      string_lit("-9223372036854775807"), 10, -9223372036854775807ll, string_empty);
  test_format_read_i64(string_lit("-123abcdef-hello"), 16, -0x123ABCDEF, string_lit("-hello"));
}
