#include "check_spec.h"
#include "core_base64.h"

spec(base64) {

  it("can encode/decode helloworld") {
    const String encoded = string_lit("SGVsbG8gV29ybGQ=");
    const String decoded = base64_decode_scratch(encoded);

    check_eq_int(base64_decoded_size(encoded), decoded.size);
    check_eq_int(base64_encoded_size(decoded.size), encoded.size);

    check_eq_string(decoded, string_lit("Hello World"));
    check_eq_string(encoded, base64_encode_scratch(decoded));
  }

  it("can encode/decode the wikipedia base64 example") {
    const String encoded =
        string_lit("TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB0aGlz"
                   "IHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIGx1c3Qgb2Yg"
                   "dGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGlu"
                   "dWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRo"
                   "ZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4=");
    const String decoded = base64_decode_scratch(encoded);

    check_eq_int(base64_decoded_size(encoded), decoded.size);
    check_eq_int(base64_encoded_size(decoded.size), encoded.size);

    check_eq_string(
        decoded,
        string_lit(
            "Man is distinguished, not only by his reason, but by this singular passion from other "
            "animals, which is a lust of the mind, that by a perseverance of delight in the "
            "continued"
            " and indefatigable generation of knowledge, exceeds the short vehemence of any carnal "
            "pleasure."));
    check_eq_string(encoded, base64_encode_scratch(decoded));
  }

  it("can encode/decode content with 2 padding characters") {
    const String encoded = string_lit("YW55IGNhcm5hbCBwbGVhc3VyZQ==");
    const String decoded = base64_decode_scratch(encoded);

    check_eq_int(base64_decoded_size(encoded), decoded.size);
    check_eq_int(base64_encoded_size(decoded.size), encoded.size);

    check_eq_string(decoded, string_lit("any carnal pleasure"));
    check_eq_string(encoded, base64_encode_scratch(decoded));
  }

  it("can encode/decode content with 1 padding character") {
    const String encoded = string_lit("YW55IGNhcm5hbCBwbGVhc3U=");
    const String decoded = base64_decode_scratch(encoded);

    check_eq_int(base64_decoded_size(encoded), decoded.size);
    check_eq_int(base64_encoded_size(decoded.size), encoded.size);

    check_eq_string(decoded, string_lit("any carnal pleasu"));
    check_eq_string(encoded, base64_encode_scratch(decoded));
  }

  it("can encode/decode content with no padding characters") {
    const String encoded = string_lit("YW55IGNhcm5hbCBwbGVhc3Vy");
    const String decoded = base64_decode_scratch(encoded);

    check_eq_int(base64_decoded_size(encoded), decoded.size);
    check_eq_int(base64_encoded_size(decoded.size), encoded.size);

    check_eq_string(decoded, string_lit("any carnal pleasur"));
    check_eq_string(encoded, base64_encode_scratch(decoded));
  }

  it("returns an emtpy string when providing invalid base64 data") {
    const String decoded = base64_decode_scratch(string_lit("SGVsbG8-gV29ybGQ"));
    check_eq_string(decoded, string_empty);
  }

  it("encodes an empty string to an empty string") {
    check_eq_int(base64_encoded_size(0), 0);
    check_eq_string(base64_encode_scratch(string_empty), string_empty);
  }

  it("returns an empty string when decoding an empty string") {
    check_eq_int(base64_decoded_size(string_empty), 0);
    const String decoded = base64_decode_scratch(string_empty);
    check_eq_string(decoded, string_empty);
  }
}
