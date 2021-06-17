#include "core_base64.h"
#include "core_diag.h"

static void test_base64_decode_wiki_example() {
  const String decoded = base64_decode_scratch(
      string_lit("TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB0aGlz"
                 "IHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIGx1c3Qgb2Yg"
                 "dGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGlu"
                 "dWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRo"
                 "ZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4="));
  diag_assert(string_eq(
      decoded,
      string_lit(
          "Man is distinguished, not only by his reason, but by this singular passion from other "
          "animals, which is a lust of the mind, that by a perseverance of delight in the continued"
          " and indefatigable generation of knowledge, exceeds the short vehemence of any carnal "
          "pleasure.")));
}

static void test_base64_decode_helloworld() {
  const String decoded = base64_decode_scratch(string_lit("SGVsbG8gV29ybGQ="));
  diag_assert(string_eq(decoded, string_lit("Hello World")));
}

static void test_base64_decode_with_2_padding_chars() {
  const String decoded = base64_decode_scratch(string_lit("YW55IGNhcm5hbCBwbGVhc3VyZQ=="));
  diag_assert(string_eq(decoded, string_lit("any carnal pleasure")));
}

static void test_base64_decode_stops_when_invalid_char_is_encountered() {
  const String decoded = base64_decode_scratch(string_lit("SGVsbG8-gV29ybGQ"));
  diag_assert(string_eq(decoded, string_lit("Hello")));
}

static void test_base64_decode_empty() {
  const String decoded = base64_decode_scratch(string_lit(""));
  diag_assert(string_eq(decoded, string_lit("")));
}

void test_base64() {
  test_base64_decode_wiki_example();
  test_base64_decode_helloworld();
  test_base64_decode_with_2_padding_chars();
  test_base64_decode_stops_when_invalid_char_is_encountered();
  test_base64_decode_empty();
}
