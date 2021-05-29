#include "core_diag.h"
#include "core_format.h"

static void test_format_write_u64(const u64 val, const u8 base, String expected) {
  Allocator* alloc = alloc_bump_create_stack(128);

  DynString string = dynstring_create(alloc, 32);
  format_write_int(&string, val, .base = base);

  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_format_write_i64(const i64 val, const u8 base, String expected) {
  Allocator* alloc = alloc_bump_create_stack(128);

  DynString string = dynstring_create(alloc, 32);
  format_write_int(&string, val, .base = base);

  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

void test_format() {
  test_format_write_u64(0, 10, string_lit("0"));
  test_format_write_u64(1, 10, string_lit("1"));
  test_format_write_u64(42, 10, string_lit("42"));
  test_format_write_u64(1337, 10, string_lit("1337"));
  test_format_write_u64(u64_max, 10, string_lit("18446744073709551615"));

  test_format_write_u64(0, 2, string_lit("0"));
  test_format_write_u64(1, 2, string_lit("1"));
  test_format_write_u64(2, 2, string_lit("10"));
  test_format_write_u64(0b010110110, 2, string_lit("10110110"));
  test_format_write_u64(255, 2, string_lit("11111111"));

  test_format_write_u64(0x0, 16, string_lit("0"));
  test_format_write_u64(0x9, 16, string_lit("9"));
  test_format_write_u64(0xF, 16, string_lit("F"));
  test_format_write_u64(0xDEADBEEF, 16, string_lit("DEADBEEF"));
  test_format_write_u64(u64_max, 16, string_lit("FFFFFFFFFFFFFFFF"));

  test_format_write_i64(0, 10, string_lit("0"));
  test_format_write_i64(1, 10, string_lit("1"));
  test_format_write_i64(-1, 10, string_lit("-1"));
  test_format_write_i64(-42, 10, string_lit("-42"));
  test_format_write_i64(1337, 10, string_lit("1337"));
  test_format_write_i64(i64_min, 10, string_lit("-9223372036854775808"));
  test_format_write_i64(i64_max, 10, string_lit("9223372036854775807"));
}
