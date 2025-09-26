#include "core/ascii.h"
#include "core/unicode.h"

bool unicode_is_control(const Unicode cp) {
  return unicode_is_ascii(cp) && ascii_is_control((u8)cp);
}
