#include "core_diag.h"
#include "core_file.h"
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
static UINT                       g_consoleCodePageOriginal;

static void tty_pal_override_input_mode(File* file, struct ConsoleModeOverride* override) {
  if (GetConsoleMode(file->handle, &override->original)) {
    DWORD newMode = override->original;
    newMode |= 0x0001; // ENABLE_PROCESSED_INPUT 0x0001
    newMode |= 0x0200; // ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
    SetConsoleMode(file->handle, newMode);
    override->enabled = true;
  }
}

static void tty_pal_override_output_mode(File* file, struct ConsoleModeOverride* override) {
  if (GetConsoleMode(file->handle, &override->original)) {
    DWORD newMode = override->original;
    newMode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    SetConsoleMode(file->handle, newMode);
    override->enabled = true;
  }
}

static void tty_pal_restore_mode(File* file, struct ConsoleModeOverride* override) {
  if (override->enabled) {
    SetConsoleMode(file->handle, override->original);
  }
}

void tty_pal_init() {
  tty_pal_override_input_mode(g_file_stdin, &g_consoleModeInputOverride);
  tty_pal_override_output_mode(g_file_stdout, &g_consoleModeOutputOverride);
  tty_pal_override_output_mode(g_file_stderr, &g_consoleModeErrorOverride);

  // Setup the console to use the utf8 code-page.
  g_consoleCodePageOriginal = GetConsoleCP();
  SetConsoleCP(CP_UTF8);
}

void tty_pal_teardown() {
  tty_pal_restore_mode(g_file_stdin, &g_consoleModeInputOverride);
  tty_pal_restore_mode(g_file_stdout, &g_consoleModeOutputOverride);
  tty_pal_restore_mode(g_file_stderr, &g_consoleModeErrorOverride);
  SetConsoleCP(g_consoleCodePageOriginal);
}

bool tty_pal_isatty(File* file) { return GetFileType(file->handle) == FILE_TYPE_CHAR; }

u16 tty_pal_width(File* file) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  const BOOL                 res = GetConsoleScreenBufferInfo(file->handle, &csbi);
  diag_assert_msg(res, "GetConsoleScreenBufferInfo() failed");
  (void)res;
  return (u16)(1 + csbi.srWindow.Right - csbi.srWindow.Left);
}

u16 tty_pal_height(File* file) {
  diag_assert_msg(tty_pal_isatty(file), "Given file is not a tty");

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  const BOOL                 res = GetConsoleScreenBufferInfo(file->handle, &csbi);
  diag_assert_msg(res, "GetConsoleScreenBufferInfo() failed");
  (void)res;
  return (u16)(1 + csbi.srWindow.Bottom - csbi.srWindow.Top);
}
