#include "core_diag.h"
#include "core_float.h"
#include "core_format.h"

static void test_format_write_u64(const u64 val, const FormatOptsInt opts, const String expected) {
  Allocator* alloc  = alloc_bump_create_stack(128);
  DynString  string = dynstring_create(alloc, 32);

  format_write_u64(&string, val, &opts);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_i64(const i64 val, const FormatOptsInt opts, const String expected) {
  Allocator* alloc  = alloc_bump_create_stack(128);
  DynString  string = dynstring_create(alloc, 32);

  format_write_i64(&string, val, &opts);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void
test_format_write_f64(const f64 val, const FormatOptsFloat opts, const String expected) {
  Allocator* alloc  = alloc_bump_create_stack(128);
  DynString  string = dynstring_create(alloc, 32);

  format_write_f64(&string, val, &opts);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_bool(const bool val, const String expected) {
  Allocator* alloc  = alloc_bump_create_stack(128);
  DynString  string = dynstring_create(alloc, 32);

  format_write_bool(&string, val);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_bitset(const BitSet val, const String expected) {
  Allocator* alloc  = alloc_bump_create_stack(128);
  DynString  string = dynstring_create(alloc, 32);

  format_write_bitset(&string, val);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_mem() {
  Allocator* alloc  = alloc_bump_create_stack(128);
  DynString  string = dynstring_create(alloc, 32);

  u64 testData = 0x8BADF00DDEADBEEF;
  Mem testMem  = mem_create(&testData, sizeof(testData));

  format_write_mem(&string, testMem);
  diag_assert(string_eq(dynstring_view(&string), string_lit("8BADF00DDEADBEEF")));

  dynstring_destroy(&string);
}

void test_format() {
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
}
