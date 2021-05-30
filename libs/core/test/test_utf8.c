#include "core_diag.h"
#include "core_utf8.h"

static String g_test_utf8_str =
    string_lit("STARGΛ̊TE,Hello world,Καλημέρα κόσμε,コンニチハ,⡌⠁⠧⠑ ⠼⠁⠒,ᚻᛖ ᚳᚹᚫᚦ ᚦᚫᛏ,ሰማይ አይታረስ "
               "ንጉሥ አይከሰስ።,แผ่นดินฮั่นเสื่อมโทรมแสนสังเวช,Зарегистрируйтесь,გთხოვთ ახლავე გაიაროთ⎪⎢⎜ "
               "⎳aⁱ-bⁱ⎟⎥⎪▁▂▃▄▅▆▇█∀∂∈ℝ∧∪≡∞");

static void test_utf8_cp_count() {
  diag_assert(utf8_cp_count(string_lit("")) == 0);
  diag_assert(utf8_cp_count(string_lit("Hello")) == 5);
  diag_assert(utf8_cp_count(g_test_utf8_str) == 184);
}

void test_utf8() { test_utf8_cp_count(); }
