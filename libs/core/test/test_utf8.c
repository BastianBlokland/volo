#include "check/spec.h"
#include "core/array.h"
#include "core/dynstring.h"
#include "core/utf8.h"

spec(utf8) {

  static String g_testStr =
      string_static("STARGΛ̊TE,Hello world,Καλημέρα κόσμε,コンニチハ,⡌⠁⠧⠑ ⠼⠁⠒,ᚻᛖ ᚳᚹᚫᚦ ᚦᚫᛏ,ሰማይ አይታረስ "
                    "ንጉሥ አይከሰስ።,แผ่นดินฮั่นเสื่อมโทรมแสนสังเวช,Зарегистрируйтесь,გთხოვთ ახლავე გაიაროთ⎪⎢⎜ "
                    "⎳aⁱ-bⁱ⎟⎥⎪▁▂▃▄▅▆▇█∀∂∈ℝ∧∪≡∞");

  static const String g_validUtf8Strs[] = {
      string_static("Hello World"),
      string_static("\xc3\xb1"),
      string_static("\xe2\x82\xa1"),
      string_static("\xf0\x90\x8c\xbc"),
  };

  static const String g_invalidUtf8Strs[] = {
      string_static("\xc3\x28"),
      string_static("\xa0\xa1"),
      string_static("\xe2\x28\xa1"),
      string_static("\xe2\x82\x28"),
      string_static("\xf0\x28\x8c\xbc"),
      string_static("\xf0\x90\x28\xbc"),
      string_static("\xf0\x28\x8c\x28"),
  };

  it("can validate utf8 strings") {
    check(utf8_validate(string_empty));
    check(utf8_validate(g_testStr));

    for (u32 i = 0; i != array_elems(g_validUtf8Strs); ++i) {
      check(utf8_validate(g_validUtf8Strs[i]));
    }
    for (u32 i = 0; i != array_elems(g_invalidUtf8Strs); ++i) {
      check(!utf8_validate(g_invalidUtf8Strs[i]));
    }
  }

  it("can count codepoints in a utf8 string") {
    check_eq_int(utf8_cp_count(string_empty), 0);
    check_eq_int(utf8_cp_count(string_lit("Hello")), 5);
    check_eq_int(utf8_cp_count(g_testStr), 184);

    check_eq_int(string_lit("Привет, мир").size, 20);
    check_eq_int(utf8_cp_count(string_lit("Привет, мир")), 11);
  }

  it("can compute the required utf8 bytes") {
    check_eq_int(utf8_cp_bytes(0x26), 1);
    check_eq_int(utf8_cp_bytes(0x39B), 2);
    check_eq_int(utf8_cp_bytes(0xE3F), 3);
    check_eq_int(utf8_cp_bytes(0x1D459), 4);
  }

  it("can compute the total utf8 bytes from the starting character") {
    check_eq_int(utf8_cp_bytes_from_first("a"[0]), 1);
    check_eq_int(utf8_cp_bytes_from_first("Λ"[0]), 2);
    check_eq_int(utf8_cp_bytes_from_first("�"[0]), 3);
    check_eq_int(utf8_cp_bytes_from_first("�"[0]), 3);
    check_eq_int(utf8_cp_bytes_from_first("𝑙"[0]), 4);
    check_eq_int(utf8_cp_bytes_from_first(0), 1);
  }

  it("can encode codepoints as utf8") {
    struct {
      Unicode cp;
      String  expected;
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
      utf8_cp_write_to(&string, data[i].cp);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can decode codepoints from utf8") {
    struct {
      String  utf8;
      Unicode expected;
      String  remaining;
    } const data[] = {
        {string_lit("\0"), 0x0, string_lit("")},
        {string_lit("a"), 0x61, string_lit("")},
        {string_lit("&"), 0x26, string_lit("")},
        {string_lit("Λ"), 0x39B, string_lit("")},
        {string_lit("฿"), 0xE3F, string_lit("")},
        {string_lit("�"), 0xFFFD, string_lit("")},
        {string_lit("⠼"), 0x283C, string_lit("")},
        {string_lit("⠼hello"), 0x283C, string_lit("hello")},
    };

    for (usize i = 0; i != array_elems(data); ++i) {
      Unicode      result;
      const String remaining = utf8_cp_read(data[i].utf8, &result);

      check_eq_string(remaining, data[i].remaining);
      check_eq_int(result, data[i].expected);
    }
  }
}
