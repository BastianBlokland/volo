#include "core_diag.h"
#include "file_internal.h"
#include "tty_internal.h"
#include <sys/ioctl.h>
#include <unistd.h>

void tty_pal_init() {}
void tty_pal_teardown() {}

bool tty_pal_isatty(File* file) { return isatty(file->handle); }

u16 tty_pal_width(File* file) {
  diag_assert_msg(tty_pal_isatty(file), string_lit("Given file is not a tty"));

  struct winsize ws;
  const int      res = ioctl(file->handle, TIOCGWINSZ, &ws);
  diag_assert_msg(res == 0, fmt_write_scratch("ioctl() failed: {}", fmt_int(res)));
  (void)res;
  return ws.ws_col;
}

u16 tty_pal_height(File* file) {
  diag_assert_msg(tty_pal_isatty(file), string_lit("Given file is not a tty"));

  struct winsize ws;
  const int      res = ioctl(file->handle, TIOCGWINSZ, &ws);
  diag_assert_msg(res == 0, fmt_write_scratch("ioctl() failed: {}", fmt_int(res)));
  (void)res;
  return ws.ws_row;
}
