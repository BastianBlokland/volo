#include "check_spec.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_deflate.h"

static String test_data_scratch(const String bitString) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, bits_to_bytes(bitString.size) + 1, 1);
  DynString str        = dynstring_create_over(scratchMem);

  u32 accum = 0, accumBits = 0;
  mem_for_u8(bitString, bitChar) {
    const u32 val = *bitChar != '0';
    accum |= val << accumBits;
    if (++accumBits == 8) {
      dynstring_append_char(&str, accum);
      accum = accumBits = 0;
    }
  }
  if (accumBits) {
    dynstring_append_char(&str, accum);
  }

  return dynstring_view(&str);
}

static void
test_decode_success(CheckTestContext* _testCtx, const String inputBits, const String expectedBits) {
  const String input = test_data_scratch(inputBits);

  Mem       outputMem    = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString outputBuffer = dynstring_create_over(outputMem);

  DeflateError err;
  const String remaining = deflate_decode(input, &outputBuffer, &err);

  check_msg(
      !remaining.size, "Remaining data {} (input: {})", fmt_bitset(remaining), fmt_bitset(input));

  check_msg(err == DeflateError_None, "Decode failed (input: {})", fmt_bitset(input));

  const String output         = dynstring_view(&outputBuffer);
  const String expectedOutput = test_data_scratch(expectedBits);
  check_msg(
      mem_eq(output, expectedOutput),
      "Output {} == {} (input: {})",
      fmt_bitset(output),
      fmt_bitset(expectedOutput),
      fmt_bitset(input));
}

static void test_decode_fail(
    CheckTestContext* _testCtx, const String inputBits, const DeflateError expectedError) {
  const String input = test_data_scratch(inputBits);

  Mem       outputMem    = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString outputBuffer = dynstring_create_over(outputMem);

  DeflateError err;
  deflate_decode(input, &outputBuffer, &err);

  check_msg(
      err == expectedError,
      "Error {} == {} (input: {})",
      fmt_int(err),
      fmt_int(expectedError),
      fmt_bitset(input));
}

spec(deflate) {
  it("successfully decodes an empty uncompressed block") {
    test_decode_success(
        _testCtx,
        string_lit("1"                /* Final */
                   "00"               /* Type */
                   "00000"            /* Alignment padding */
                   "0000000000000000" /* Length */
                   "1111111111111111" /* Length inverted */),
        string_empty);
  }

  it("successfully decodes an uncompressed block") {
    test_decode_success(
        _testCtx,
        string_lit("1"                /* Final */
                   "00"               /* Type */
                   "00000"            /* Alignment padding */
                   "1100000000000000" /* Length */
                   "0011111111111111" /* Length inverted */
                   "101010101010101010101010" /* Data */),
        string_lit("101010101010101010101010"));
  }

  it("successfully decodes multiple uncompressed blocks") {
    test_decode_success(
        _testCtx,
        string_lit("0"                /* Final */
                   "00"               /* Type */
                   "00000"            /* Alignment padding */
                   "1000000000000000" /* Length */
                   "0111111111111111" /* Length inverted */
                   "10101010"         /* Data */
                   "1"                /* Final */
                   "00"               /* Type */
                   "00000"            /* Alignment padding */
                   "1000000000000000" /* Length */
                   "0111111111111111" /* Length inverted */
                   "01010101" /* Data */),
        string_lit("1010101001010101"));
  }

  it("fails to decode on empty input") {
    test_decode_fail(_testCtx, string_lit(""), DeflateError_Truncated);
  }

  it("fails to decode when block-type is missing") {
    test_decode_fail(_testCtx, string_lit("1" /* Final */), DeflateError_Truncated);
  }

  it("fails to decode an invalid block-type") {
    test_decode_fail(
        _testCtx,
        string_lit("1" /* Final */
                   "11" /* Type */),
        DeflateError_Malformed);
  }

  it("fails to decode when missing a final block") {
    test_decode_fail(
        _testCtx,
        string_lit("0" /* Final */
                   "11" /* Type */),
        DeflateError_Malformed);
  }

  it("fails to decode an uncompressed block with mismatched nlen") {
    test_decode_fail(
        _testCtx,
        string_lit("1"                /* Final */
                   "00"               /* Type */
                   "00000"            /* Alignment padding */
                   "1100000000000000" /* Length */
                   "0111111111111111" /* Length inverted */
                   "1010101010101010" /* Data */),
        DeflateError_Malformed);
  }

  it("fails to decode an uncompressed block with missing nlen") {
    test_decode_fail(
        _testCtx,
        string_lit("1"     /* Final */
                   "00"    /* Type */
                   "00000" /* Alignment padding */
                   "1100000000000000" /* Length */),
        DeflateError_Truncated);
  }

  it("fails to decode a truncated uncompressed block") {
    test_decode_fail(
        _testCtx,
        string_lit("1"                /* Final */
                   "00"               /* Type */
                   "00000"            /* Alignment padding */
                   "1100000000000000" /* Length */
                   "0011111111111111" /* Length inverted */
                   "1010101010101010" /* Data */),
        DeflateError_Truncated);
  }
}
