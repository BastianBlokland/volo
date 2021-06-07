#include "core_diag.h"
#include "core_utf8.h"
#include "core_winutils.h"

#ifdef VOLO_WIN32
static void test_winutils_from_widestr() {
  static String testStr =
      string_static("STARGΛ̊TE,Hello world,Καλημέρα κόσμε,コンニチハ,⡌⠁⠧⠑ ⠼⠁⠒,ᚻᛖ ᚳᚹᚫᚦ ᚦᚫᛏ,ሰማይ አይታረስ "
                    "ንጉሥ አይከሰስ።,แผ่นดินฮั่นเสื่อมโทรมแสนสังเวช,Зарегистрируйтесь,გთხოვთ ახლავე გაიაროთ⎪⎢⎜ "
                    "⎳aⁱ-bⁱ⎟⎥⎪▁▂▃▄▅▆▇█∀∂∈ℝ∧∪≡∞");

  const usize wideCharsSize = winutils_to_widestr_size(testStr);
  diag_assert(wideCharsSize == 368 + 1); // +1 for null-terminator.

  Mem         wideChars     = mem_stack(wideCharsSize);
  const usize wideCharCount = winutils_to_widestr(wideChars, testStr);

  diag_assert(*(mem_end(wideChars) - 1) == '\0');
  diag_assert(winutils_from_widestr_size(wideChars.ptr, wideCharCount) == testStr.size);

  Mem         utf8     = mem_stack(winutils_from_widestr_size(wideChars.ptr, wideCharCount));
  const usize utf8Size = winutils_from_widestr(utf8, wideChars.ptr, wideCharCount);

  diag_assert(utf8Size == testStr.size);
  diag_assert(mem_eq(utf8, testStr));
}
#endif

void test_winutils() {
#ifdef VOLO_WIN32
  test_winutils_from_widestr();
#endif
}
