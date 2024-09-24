#include "check_spec.h"
#include "core_alloc.h"
#include "core_base64.h"
#include "core_deflate.h"

static void test_decode_success(
    CheckTestContext* _testCtx, const String inputBase64, const String expectedBase64) {
  const String input = base64_decode_scratch(inputBase64);

  Mem       outputMem    = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString outputBuffer = dynstring_create_over(outputMem);

  DeflateError err;
  const String remaining       = deflate_decode(input, &outputBuffer, &err);
  const String remainingBase64 = base64_encode_scratch(remaining);

  check_eq_string(remainingBase64, string_empty);
  check_eq_int(err, DeflateError_None);

  String outputBase64 = base64_encode_scratch(dynstring_view(&outputBuffer));
  check_eq_string(outputBase64, expectedBase64);

  dynstring_destroy(&outputBuffer);
}

static void test_decode_fail(
    CheckTestContext* _testCtx, const String inputBase64, const DeflateError expectedError) {
  const String input = base64_decode_scratch(inputBase64);

  Mem       outputMem    = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString outputBuffer = dynstring_create_over(outputMem);

  DeflateError err;
  deflate_decode(input, &outputBuffer, &err);

  check_eq_int(err, expectedError);

  dynstring_destroy(&outputBuffer);
}

spec(deflate) {
  it("successfully decodes an empty uncompressed block") {
    test_decode_success(_testCtx, string_lit("gAAA//8="), string_empty);
  }

  it("fails to decode on empty input") {
    test_decode_fail(_testCtx, string_empty, DeflateError_Unknown);
  }
}
