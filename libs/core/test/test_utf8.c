#include "core_array.h"
#include "core_utf8.h"

#include "check_spec.h"

spec(utf8) {

  static String testStr =
      string_static("STARGΛ̊TE,Hello world,Καλημέρα κόσμε,コンニチハ,⡌⠁⠧⠑ ⠼⠁⠒,ᚻᛖ ᚳᚹᚫᚦ ᚦᚫᛏ,ሰማይ አይታረስ "
                    "ንጉሥ አይከሰስ።,แผ่นดินฮั่นเสื่อมโทรมแสนสังเวช,Зарегистрируйтесь,გთხოვთ ახლავე გაიაროთ⎪⎢⎜ "
                    "⎳aⁱ-bⁱ⎟⎥⎪▁▂▃▄▅▆▇█∀∂∈ℝ∧∪≡∞");

  it("can count codepoints in a utf8 string") {
    check_eq_int(utf8_cp_count(string_empty), 0);
    check_eq_int(utf8_cp_count(string_lit("Hello")), 5);
    check_eq_int(utf8_cp_count(testStr), 184);
  }

  it("can compute the required utf8 bytes") {
    check_eq_int(utf8_cp_bytes(0x26), 1);
    check_eq_int(utf8_cp_bytes(0x39B), 2);
    check_eq_int(utf8_cp_bytes(0xE3F), 3);
    check_eq_int(utf8_cp_bytes(0x1D459), 4);
  }

  it("can encode codepoints as utf8") {
    struct {
      Utf8Codepoint cp;
      String        expected;
    } const data[] = {
        {0x0, string_lit("\0")},
        {0x61, string_lit("a")},
        {0x26, string_lit("&")},
        {0x39B, string_lit("Λ")},
        {0xE3F, string_lit("฿")},
        {0xFFFD, string_lit("�")},
        {0x283C, string_lit("⠼")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      utf8_cp_write(&string, data[i].cp);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }
}
