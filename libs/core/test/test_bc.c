#include "check_spec.h"
#include "core_array.h"
#include "core_bc.h"
#include "core_math.h"

#define test_threshold_color8888 15

static void test_bc0_block_fill(Bc0Block* b, const BcColor8888 color) {
  for (u32 i = 0; i != array_elems(b->colors); ++i) {
    b->colors[i] = color;
  }
}

static void test_bc0_block_fill_checker(Bc0Block* b, const BcColor8888 cA, const BcColor8888 cB) {
  for (u32 y = 0; y != 4; ++y) {
    for (u32 x = 0; x != 4; ++x) {
      b->colors[y * 4 + x] = ((x & 1) == (y & 1)) ? cA : cB;
    }
  }
}

static void test_color8888_check(
    CheckTestContext* ctx, const BcColor8888 a, const BcColor8888 b, const SourceLoc src) {
  if (UNLIKELY(
          math_abs((i32)a.r - (i32)b.r) > test_threshold_color8888 ||
          math_abs((i32)a.g - (i32)b.g) > test_threshold_color8888 ||
          math_abs((i32)a.b - (i32)b.b) > test_threshold_color8888 ||
          math_abs((i32)a.a - (i32)b.a) > test_threshold_color8888)) {
    check_report_error(
        ctx,
        fmt_write_scratch(
            "{} == {}",
            fmt_list_lit(fmt_int(a.r), fmt_int(a.g), fmt_int(a.b), fmt_int(a.a)),
            fmt_list_lit(fmt_int(b.r), fmt_int(b.g), fmt_int(b.b), fmt_int(b.a))),
        src);
  }
}

#define check_eq_color8888(_A_, _B_) test_color8888_check(_testCtx, (_A_), (_B_), source_location())

spec(bc) {
  static const BcColor8888 g_black = {0, 0, 0, 255};
  static const BcColor8888 g_white = {255, 255, 255, 255};

  it("can encode a black bc1 block") {
    Bc0Block orgBlock;
    test_bc0_block_fill(&orgBlock, g_black);

    Bc1Block bc1Block;
    bc1_encode(&orgBlock, &bc1Block);

    Bc0Block decodedBlock;
    bc1_decode(&bc1Block, &decodedBlock);

    for (u32 i = 0; i != array_elems(decodedBlock.colors); ++i) {
      check_eq_color8888(decodedBlock.colors[i], g_black);
    }
  }

  it("can encode a white bc1 block") {
    Bc0Block orgBlock;
    test_bc0_block_fill(&orgBlock, g_white);

    Bc1Block bc1Block;
    bc1_encode(&orgBlock, &bc1Block);

    Bc0Block decodedBlock;
    bc1_decode(&bc1Block, &decodedBlock);

    for (u32 i = 0; i != array_elems(decodedBlock.colors); ++i) {
      check_eq_color8888(decodedBlock.colors[i], g_white);
    }
  }

  it("can encode a black and white checker bc1 block") {
    Bc0Block orgBlock;
    test_bc0_block_fill_checker(&orgBlock, g_black, g_white);

    Bc1Block bc1Block;
    bc1_encode(&orgBlock, &bc1Block);

    Bc0Block decodedBlock;
    bc1_decode(&bc1Block, &decodedBlock);

    for (u32 i = 0; i != array_elems(decodedBlock.colors); ++i) {
      check_eq_color8888(decodedBlock.colors[i], orgBlock.colors[i]);
    }
  }
}
