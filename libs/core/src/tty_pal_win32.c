#include "core_array.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_math.h"
#include "core_winutils.h"

#include "file_internal.h"
#include "tty_internal.h"

#include <Windows.h>

struct ConsoleModeOverride {
  bool  enabled;
  DWORD original;
};

static struct ConsoleModeOverride g_consoleModeInputOverride;
static struct ConsoleModeOverride g_consoleModeOutputOverride;
static struct ConsoleModeOverride g_consoleModeErrorOverride;
static UINT                       g_consoleInputCodePageOriginal;
static UINT                       g_consoleOutputCodePageOriginal;

static void tty_pal_override_input_mode(File* file, struct ConsoleModeOverride* override) {
  if (GetConsoleMode(file->handle, &override->original)) {
    DWORD newMode = override->original;
    newMode |= ENABLE_PROCESSED_INPUT;
    newMode |= 0x0200; // ENABLE_VIRTUAL_TERMINAL_INPUT

    SetConsoleMode(file->handle, newMode);
    override->enabled = true;
  }
}

static void tty_pal_override_output_mode(File* file, struct ConsoleModeOverride* override) {
  if (GetConsoleMode(file->handle, &override->original)) {
    DWORD newMode = override->original;
    newMode |= ENABLE_PROCESSED_OUTPUT;
    newMode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING

    SetConsoleMode(file->handle, newMode);
    override->enabled = true;
  }
}

static void tty_pal_restore_mode(File* file, struct ConsoleModeOverride* override) {
  if (override->enabled) {
    SetConsoleMode(file->handle, override->original);
  }
}

static bool tty_pal_has_key_input(File* file) {
  DWORD      eventCount;
  const BOOL getNumRes = GetNumberOfConsoleInputEvents(file->handle, &eventCount);
  if (UNLIKELY(!getNumRes)) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "GetNumberOfConsoleInputEvents() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }

  if (!eventCount) {
    return false; // No events at all.
  }

  INPUT_RECORD records[256];
  DWORD        peekCount;
  const BOOL   peekRes = PeekConsoleInput(file->handle, records, array_elems(records), &peekCount);
  if (UNLIKELY(!peekRes)) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "PeekConsoleInput() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }

  // Search in the peeked events for a key-down event.
  for (usize i = 0; i != peekCount; ++i) {
    switch (records[i].EventType) {
    case KEY_EVENT:
      if (!records[i].Event.KeyEvent.bKeyDown) {
        break; // Ignore 'KeyUp' events.
      }
      return true;
    case MOUSE_EVENT:
    case WINDOW_BUFFER_SIZE_EVENT:
    case FOCUS_EVENT:
    case MENU_EVENT:
      break; // Unsupported event.
    default:
      diag_crash_msg("Unkown console event-type: {}", fmt_int((u32)records[i].EventType));
    }
  }
  return false; // No key events found.
}

void tty_pal_init() {
  tty_pal_override_input_mode(g_file_stdin, &g_consoleModeInputOverride);
  tty_pal_override_output_mode(g_file_stdout, &g_consoleModeOutputOverride);
  tty_pal_override_output_mode(g_file_stderr, &g_consoleModeErrorOverride);

  // Setup the console to use the utf8 code-page.
  g_consoleInputCodePageOriginal  = GetConsoleCP();
  g_consoleOutputCodePageOriginal = GetConsoleOutputCP();

  SetConsoleCP(CP_UTF8);
  SetConsoleOutputCP(CP_UTF8);
}

void tty_pal_teardown() {
  tty_pal_restore_mode(g_file_stdin, &g_consoleModeInputOverride);
  tty_pal_restore_mode(g_file_stdout, &g_consoleModeOutputOverride);
  tty_pal_restore_mode(g_file_stderr, &g_consoleModeErrorOverride);

  SetConsoleCP(g_consoleInputCodePageOriginal);
  SetConsoleOutputCP(g_consoleOutputCodePageOriginal);
}

bool tty_pal_isatty(File* file) { return GetFileType(file->handle) == FILE_TYPE_CHAR; }

u16 tty_pal_width(File* file) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  const BOOL                 res = GetConsoleScreenBufferInfo(file->handle, &csbi);
  if (UNLIKELY(!res)) {
    diag_crash_msg("GetConsoleScreenBufferInfo() failed");
  }
  return (u16)(1 + csbi.srWindow.Right - csbi.srWindow.Left);
}

u16 tty_pal_height(File* file) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  const BOOL                 res = GetConsoleScreenBufferInfo(file->handle, &csbi);
  if (UNLIKELY(!res)) {
    diag_crash_msg("GetConsoleScreenBufferInfo() failed");
  }
  return (u16)(1 + csbi.srWindow.Bottom - csbi.srWindow.Top);
}

void tty_pal_opts_set(File* file, const TtyOpts opts) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");
  diag_assert_msg(file->access & FileAccess_Read, "Tty handle does not have read access");

  DWORD      mode   = 0;
  const BOOL getRes = GetConsoleMode(file->handle, &mode);
  if (UNLIKELY(!getRes)) {
    diag_crash_msg("GetConsoleMode() failed");
  }

  if (opts & TtyOpts_NoEcho) {
    mode &= ~ENABLE_ECHO_INPUT;
  } else {
    mode |= ENABLE_ECHO_INPUT;
  }

  if (opts & TtyOpts_NoBuffer) {
    mode &= ~ENABLE_LINE_INPUT;
  } else {
    mode |= ENABLE_LINE_INPUT;
  }

  const BOOL setRes = SetConsoleMode(file->handle, mode);
  if (UNLIKELY(!setRes)) {
    diag_crash_msg("SetConsoleMode() failed");
  }
}

bool tty_pal_read(File* file, DynString* dynstr, const TtyReadFlags flags) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");
  diag_assert_msg(file->access & FileAccess_Read, "Tty handle does not have read access");

  if ((flags & TtyReadFlags_NoBlock) && !tty_pal_has_key_input(file)) {
    return false; // No keyboard input is available for reading at the given console.
  }

  static const DWORD g_maxChars = 512;

  Mem        wideBuffer = mem_stack(g_maxChars * sizeof(wchar_t));
  DWORD      wideCharsRead;
  const BOOL readRes = ReadConsole(file->handle, wideBuffer.ptr, g_maxChars, &wideCharsRead, null);
  if (UNLIKELY(!readRes)) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "ReadConsole() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }
  if (!wideCharsRead) {
    return false;
  }

  dynstring_append(dynstr, winutils_from_widestr_scratch(wideBuffer.ptr, wideCharsRead));
  return true;
}
