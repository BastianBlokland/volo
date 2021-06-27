#include "core_utf8.h"
#include "core_winutils.h"

#include "check_spec.h"

spec(winutils) {

#ifdef VOLO_WIN32
  it("can convert a widestr to utf8") {
    static String testStr = string_static(
        "STARGΛ̊TE,Hello world,Καλημέρα κόσμε,コンニチハ,⡌⠁⠧⠑ ⠼⠁⠒,ᚻᛖ ᚳᚹᚫᚦ ᚦᚫᛏ,ሰማይ አይታረስ "
        "ንጉሥ አይከሰስ።,แผ่นดินฮั่นเสื่อมโทรมแสนสังเวช,Зарегистрируйтесь,გთხოვთ ახლავე გაიაროთ⎪⎢⎜ "
        "⎳aⁱ-bⁱ⎟⎥⎪▁▂▃▄▅▆▇█∀∂∈ℝ∧∪≡∞");

    const usize wideCharsSize = winutils_to_widestr_size(testStr);
    check_eq_int(wideCharsSize, 368 + 1); // +1 for null-terminator.

    Mem         wideChars     = mem_stack(wideCharsSize);
    const usize wideCharCount = winutils_to_widestr(wideChars, testStr);

    check(*(mem_end(wideChars) - 1) == '\0');
    check_eq_int(winutils_from_widestr_size(wideChars.ptr, wideCharCount), testStr.size);

    Mem         utf8     = mem_stack(winutils_from_widestr_size(wideChars.ptr, wideCharCount));
    const usize utf8Size = winutils_from_widestr(utf8, wideChars.ptr, wideCharCount);

    check_eq_int(utf8Size, testStr.size);
    check(mem_eq(utf8, testStr));
  }
#endif
}
