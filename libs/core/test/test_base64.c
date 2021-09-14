#include "core_base64.h"

#include "check_spec.h"

spec(base64) {

  it("can decode helloworld") {
    const String decoded = base64_decode_scratch(string_lit("SGVsbG8gV29ybGQ="));
    check_eq_string(decoded, string_lit("Hello World"));
  }

  it("can decode the wikipedia base64 example") {
    const String decoded = base64_decode_scratch(
        string_lit("TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB0aGlz"
                   "IHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIGx1c3Qgb2Yg"
                   "dGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGlu"
                   "dWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRo"
                   "ZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4="));
    check_eq_string(
        decoded,
        string_lit(
            "Man is distinguished, not only by his reason, but by this singular passion from other "
            "animals, which is a lust of the mind, that by a perseverance of delight in the "
            "continued"
            " and indefatigable generation of knowledge, exceeds the short vehemence of any carnal "
            "pleasure."));
  }

  it("can decode content with 2 padding characters") {
    const String decoded = base64_decode_scratch(string_lit("YW55IGNhcm5hbCBwbGVhc3VyZQ=="));
    check_eq_string(decoded, string_lit("any carnal pleasure"));
  }

  it("stops decoding with an invalid character is encountered") {
    const String decoded = base64_decode_scratch(string_lit("SGVsbG8-gV29ybGQ"));
    check_eq_string(decoded, string_lit("Hello"));
  }

  it("returns an empty string when decoding an empty string") {
    const String decoded = base64_decode_scratch(string_empty);
    check_eq_string(decoded, string_empty);
  }
}
