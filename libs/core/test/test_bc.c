#include "check_spec.h"
#include "core_array.h"
#include "core_bc.h"

static void test_bc0_block_fill(Bc0Block* b, const BcColor8888 color) {
  for (u32 i = 0; i != array_elems(b->colors); ++i) {
    b->colors[i] = color;
  }
}

static void test_color8888_check(
    CheckTestContext* ctx, const BcColor8888 a, const BcColor8888 b, const SourceLoc src) {
  if (UNLIKELY(a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a)) {
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
}
