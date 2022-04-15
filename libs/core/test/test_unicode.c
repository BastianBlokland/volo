#include "check_spec.h"
#include "core_unicode.h"

spec(unicode) {

  it("can check if a codepoint is ascii") {
    check(unicode_is_ascii('a'));
    check(unicode_is_ascii(Unicode_HorizontalTab));
    check(unicode_is_ascii(127));
    check(!unicode_is_ascii(Unicode_ZeroWidthSpace));
  }
}
