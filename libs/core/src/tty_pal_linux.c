#include "core_array.h"
#include "core_diag.h"

#include "file_internal.h"
#include "tty_internal.h"

#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

void tty_pal_init() {}
void tty_pal_teardown() {}

bool tty_pal_isatty(File* file) { return isatty(file->handle); }

u16 tty_pal_width(File* file) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");

  struct winsize ws;
  const int      res = ioctl(file->handle, TIOCGWINSZ, &ws);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("ioctl() failed: {}", fmt_int(res));
  }
  return ws.ws_col;
}

u16 tty_pal_height(File* file) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");

  struct winsize ws;
  const int      res = ioctl(file->handle, TIOCGWINSZ, &ws);
  if (UNLIKELY(res != 0)) {
    diag_crash_msg("ioctl() failed: {}", fmt_int(res));
  }
  return ws.ws_row;
}

void tty_pal_opts_set(File* file, const TtyOpts opts) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");
  diag_assert_msg(file->access & FileAccess_Read, "Tty handle does not have read access");

  struct termios t;
  const int      getAttrRes = tcgetattr(file->handle, &t);
  if (UNLIKELY(getAttrRes != 0)) {
    diag_crash_msg("tcgetattr() failed: {}", fmt_int(getAttrRes));
  }

  if (opts & TtyOpts_NoEcho) {
    t.c_lflag &= ~ECHO;
  } else {
    t.c_lflag |= ECHO;
  }

  if (opts & TtyOpts_NoBuffer) {
    t.c_lflag &= ~ICANON;
  } else {
    t.c_lflag |= ICANON;
  }

  const int setAttrRes = tcsetattr(file->handle, TCSANOW, &t);
  if (UNLIKELY(setAttrRes != 0)) {
    diag_crash_msg("tcsetattr() failed: {}", fmt_int(setAttrRes));
  }
}

bool tty_pal_read(File* file, DynString* dynstr, const TtyReadFlags flags) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");
  diag_assert_msg(file->access & FileAccess_Read, "Tty handle does not have read access");

  if (flags & TtyReadFlags_NoBlock) {
    struct pollfd fds[1];
    fds[0].fd     = file->handle;
    fds[0].events = POLLIN;

    const int pollRet = poll(fds, array_elems(fds), 0);
    if (UNLIKELY(pollRet < 0)) {
      diag_crash_msg("poll() failed: {}, errno: {}", fmt_int(pollRet), fmt_int(errno));
    }
    if (pollRet == 0) {
      return false; // No data is available for reading at the given file.
    }
  }

  const FileResult res = file_read_sync(file, dynstr);
  if (UNLIKELY(res != FileResult_Success)) {
    diag_crash_msg("Failed to read from tty: {}", fmt_text(file_result_str(res)));
  }
  return true;
}
