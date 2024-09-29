#include "check_spec.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_deflate.h"

static String test_data_scratch(const String bitString) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, bits_to_bytes(bitString.size) + 1, 1);
  DynString str        = dynstring_create_over(scratchMem);

  u32 accum = 0, accumBits = 0;
  mem_for_u8(bitString, bitChar) {
    if (*bitChar == ' ') {
      continue;
    }
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
      !remaining.size,
      "Remaining data [{}] (input: [{}])",
      fmt_bitset(remaining, .order = FormatBitsetOrder_LeastToMostSignificant),
      fmt_bitset(input, .order = FormatBitsetOrder_LeastToMostSignificant));

  check_msg(
      err == DeflateError_None,
      "Decode failed (input: [{}])",
      fmt_bitset(input, .order = FormatBitsetOrder_LeastToMostSignificant));

  const String output         = dynstring_view(&outputBuffer);
  const String expectedOutput = test_data_scratch(expectedBits);
  check_msg(
      mem_eq(output, expectedOutput),
      "Output [{}] ({} bytes) == [{}] ({} bytes) (input: [{}])",
      fmt_bitset(output, .order = FormatBitsetOrder_LeastToMostSignificant),
      fmt_int(output.size),
      fmt_bitset(expectedOutput, .order = FormatBitsetOrder_LeastToMostSignificant),
      fmt_int(expectedOutput.size),
      fmt_bitset(input, .order = FormatBitsetOrder_LeastToMostSignificant));
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
      fmt_bitset(input, .order = FormatBitsetOrder_LeastToMostSignificant));
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

  it("successfully decodes an uncompressed block without any padding after a fixed huffman block") {
    test_decode_success(
        _testCtx,
        string_lit("0"                /* Final */
                   "10"               /* Type */
                   "110010000"        /* Literal 144 */
                   "111000000"        /* Literal 192 */
                   "111111111"        /* Literal 255 */
                   "0000000"          /* End Symbol */
                   "1"                /* Final */
                   "00"               /* Type */
                   "1100000000000000" /* Length */
                   "0011111111111111" /* Length inverted */
                   "101010101010101010101010" /* Data */),
        string_lit("00001001" /* 144 */
                   "00000011" /* 192 */
                   "11111111" /* 255 */
                   "101010101010101010101010" /* Uncompressed data */));
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

  it("successfully decodes an empty fixed huffman block") {
    test_decode_success(
        _testCtx,
        string_lit("1"  /* Final */
                   "10" /* Type */
                   "0000000" /* End symbol */),
        string_empty);
  }

  it("successfully decodes a fixed huffman block using literal symbols") {
    test_decode_success(
        _testCtx,
        string_lit("1"         /* Final */
                   "10"        /* Type */
                   "00110000"  /* Literal 0 */
                   "00110001"  /* Literal 1 */
                   "10110000"  /* Literal 128 */
                   "10111111"  /* Literal 143 */
                   "110010000" /* Literal 144 */
                   "111000000" /* Literal 192 */
                   "111111111" /* Literal 255 */
                   "0000000" /* End symbol */),
        string_lit("00000000" /* 0 */
                   "10000000" /* 1 */
                   "00000001" /* 128 */
                   "11110001" /* 143 */
                   "00001001" /* 144 */
                   "00000011" /* 192 */
                   "11111111" /* 255 */));
  }

  it("successfully decodes a fixed huffman block using a run length") {
    test_decode_success(
        _testCtx,
        string_lit("1"        /* Final */
                   "10"       /* Type */
                   "00110001" /* Literal 1 */
                   "0000010"  /* Symbol 258 (run length of 4) */
                   "00000"    /* Symbol 0 (run distance of 1) */
                   "0000000" /* End symbol */),
        string_lit("10000000" /* 1 */
                   "10000000" /* 1 */
                   "10000000" /* 1 */
                   "10000000" /* 1 */
                   "10000000" /* 1 */));
  }

  it("successfully decodes a fixed huffman block using a run length of distance 2") {
    test_decode_success(
        _testCtx,
        string_lit("1"        /* Final */
                   "10"       /* Type */
                   "10111110" /* Literal 142 */
                   "10111111" /* Literal 143 */
                   "0000011"  /* Symbol 259 (run length of 5) */
                   "00001"    /* Symbol 1 (run distance of 2) */
                   "0000000" /* End symbol */),
        string_lit("01110001" /* 142 */
                   "11110001" /* 143 */
                   "01110001" /* 142 */
                   "11110001" /* 143 */
                   "01110001" /* 142 */
                   "11110001" /* 143 */
                   "01110001" /* 142 */));
  }

  it("successfully decodes a fixed huffman block using overlapping run length") {
    test_decode_success(
        _testCtx,
        string_lit("1"        /* Final */
                   "10"       /* Type */
                   "00110000" /* Literal 0 */
                   "00110001" /* Literal 1 */
                   "00110010" /* Literal 2 */
                   "0000001"  /* Symbol 257 (run length of 3) */
                   "00010"    /* Symbol 2 (run distance of 3) */
                   "0000000" /* End symbol */),
        string_lit("00000000" /* 0 */
                   "10000000" /* 1 */
                   "01000000" /* 2 */
                   "00000000" /* 0 */
                   "10000000" /* 1 */
                   "01000000" /* 2 */));
  }

  it("fails to decode a fixed huffman block using length symbol 286") {
    test_decode_fail(
        _testCtx,
        string_lit("1"  /* Final */
                   "10" /* Type */
                   "11000110" /* Symbol 286 */),
        DeflateError_Malformed);
  }

  it("fails to decode a fixed huffman block using length symbol 287") {
    test_decode_fail(
        _testCtx,
        string_lit("1"  /* Final */
                   "10" /* Type */
                   "11000111" /* Symbol 287 */),
        DeflateError_Malformed);
  }

  it("fails to decode a fixed huffman block using distance symbol 30") {
    test_decode_fail(
        _testCtx,
        string_lit("1"        /* Final */
                   "10"       /* Type */
                   "00110000" /* Literal 0 */
                   "0000001"  /* Symbol 257 (run length of 3) */
                   "11110" /* Symbol 30 */),
        DeflateError_Malformed);
  }

  it("fails to decode a fixed huffman block using distance symbol 31") {
    test_decode_fail(
        _testCtx,
        string_lit("1"        /* Final */
                   "10"       /* Type */
                   "00110000" /* Literal 0 */
                   "0000001"  /* Symbol 257 (run length of 3) */
                   "11111" /* Symbol 31 */),
        DeflateError_Malformed);
  }

  it("fails to decode a fixed huffman block with truncated data") {
    test_decode_fail(
        _testCtx,
        string_lit("1"  /* Final */
                   "10" /* Type */
                   "00000" /* Truncated symbol */),
        DeflateError_Truncated);
  }

  it("fails to decode a fixed huffman block with truncated length extension bits") {
    test_decode_fail(
        _testCtx,
        string_lit("1"        /* Final */
                   "10"       /* Type */
                   "00110000" /* Literal 0 */
                   "0001101"  /* Symbol 269 */
                   "1" /* Truncated extension bits */),
        DeflateError_Truncated);
  }

  it("fails to decode a fixed huffman block with truncated distance extension bits") {
    test_decode_fail(
        _testCtx,
        string_lit("1"        /* Final */
                   "10"       /* Type */
                   "00110000" /* Literal 0 */
                   "11000101" /* Symbol 285 */
                   "00000"    /* Symbol 0 */
                   "0000001"  /* Symbol 257 */
                   "01000"    /* Symbol 8 */
                   "00" /* Truncated extension bits */),
        DeflateError_Truncated);
  }
}
