#pragma once
#include "core.h"
#include "core_unicode.h"

#define tty_esc "\33"

/**
 * TTY - TeleTypeWriter.
 * Contains utilities for interacting with the terminal.
 */

/**
 * Terminal foreground color.
 */
typedef enum {
  TtyFgColor_None          = 0,
  TtyFgColor_Default       = 39,
  TtyFgColor_Black         = 30,
  TtyFgColor_Red           = 31,
  TtyFgColor_Green         = 32,
  TtyFgColor_Yellow        = 33,
  TtyFgColor_Blue          = 34,
  TtyFgColor_Magenta       = 35,
  TtyFgColor_Cyan          = 36,
  TtyFgColor_White         = 37,
  TtyFgColor_BrightBlack   = 90,
  TtyFgColor_BrightRed     = 91,
  TtyFgColor_BrightGreen   = 92,
  TtyFgColor_BrightYellow  = 93,
  TtyFgColor_BrightBlue    = 94,
  TtyFgColor_BrightMagenta = 95,
  TtyFgColor_BrightCyan    = 96,
  TtyFgColor_BrightWhite   = 97,
} TtyFgColor;

/**
 * Terminal background color.
 */
typedef enum {
  TtyBgColor_None          = 0,
  TtyBgColor_Default       = 49,
  TtyBgColor_Black         = 40,
  TtyBgColor_Red           = 41,
  TtyBgColor_Green         = 42,
  TtyBgColor_Yellow        = 43,
  TtyBgColor_Blue          = 44,
  TtyBgColor_Magenta       = 45,
  TtyBgColor_Cyan          = 46,
  TtyBgColor_White         = 47,
  TtyBgColor_BrightBlack   = 100,
  TtyBgColor_BrightRed     = 101,
  TtyBgColor_BrightGreen   = 102,
  TtyBgColor_BrightYellow  = 103,
  TtyBgColor_BrightBlue    = 104,
  TtyBgColor_BrightMagenta = 105,
  TtyBgColor_BrightCyan    = 106,
  TtyBgColor_BrightWhite   = 107
} TtyBgColor;

/**
 * Special terminal style flags.
 * NOTE: Not all terminals support all options.
 * Maps closely to the ANSI SGR (Select Graphic Rendition) parameters.
 */
typedef enum {
  TtyStyleFlags_None      = 0,
  TtyStyleFlags_Bold      = 1 << 0,
  TtyStyleFlags_Faint     = 1 << 1,
  TtyStyleFlags_Italic    = 1 << 2,
  TtyStyleFlags_Underline = 1 << 3,
  TtyStyleFlags_Blink     = 1 << 4,
  TtyStyleFlags_Reversed  = 1 << 5,
} TtyStyleFlags;

/**
 * Structure representing a terminal style.
 * NOTE: A default constructed TtyStyle (all zeroes) will create a reset-to-default style.
 * Maps closely to the ANSI SGR (Select Graphic Rendition) parameters.
 */
typedef struct {
  TtyFgColor    fgColor;
  TtyBgColor    bgColor;
  TtyStyleFlags flags;
} TtyStyle;

/**
 * Construct a TtyStyle structure.
 * NOTE: Providing no arguments will create a reset-to-default style.
 */
#define ttystyle(...)                                                                              \
  ((TtyStyle){                                                                                     \
      .fgColor = TtyFgColor_None,                                                                  \
      .bgColor = TtyBgColor_None,                                                                  \
      .flags   = TtyStyleFlags_None,                                                               \
      __VA_ARGS__})

/**
 * Check if the given file is a tty.
 */
bool tty_isatty(File*);

/**
 * Retrieve the width of the given tty terminal.
 * Pre-condition: tty_isatty(file)
 */
u16 tty_width(File*);

/**
 * Retrieve the height of the given tty terminal.
 * Pre-condition: tty_isatty(file)
 */
u16 tty_height(File*);

typedef enum {
  TtyOpts_None      = 0,
  TtyOpts_NoEcho    = 1 << 0,
  TtyOpts_NoBuffer  = 1 << 1,
  TtyOpts_NoSignals = 1 << 2, // Disable signal sending, eg. enables reading ctrl-c as input.
} TtyOpts;

/**
 * Set terminal configuration options.
 *
 * Pre-condition: tty_isatty(file)
 * Pre-condition: file is opened with read access.
 */
void tty_opts_set(File*, TtyOpts);

typedef enum {
  TtyReadFlags_None    = 0,
  TtyReadFlags_NoBlock = 1 << 0,
} TtyReadFlags;

/**
 * Read all available input from the terminal into the dynamic-string.
 * If input was read 'true' is returned otherwise 'false'.
 *
 * Pre-condition: tty_isatty(file)
 * Pre-condition: file is opened with read access.
 */
bool tty_read(File*, DynString*, TtyReadFlags);

typedef enum {
  TtyInputType_Accept,
  TtyInputType_Interrupt,
  TtyInputType_KeyEscape,
  TtyInputType_KeyUp,
  TtyInputType_KeyDown,
  TtyInputType_KeyRight,
  TtyInputType_KeyLeft,
  TtyInputType_KeyEnd,
  TtyInputType_KeyHome,
  TtyInputType_KeyDelete,
  TtyInputType_KeyBackspace,
  TtyInputType_Text,
  TtyInputType_Unsupported,
  TtyInputType_End,
} TtyInputType;

typedef struct {
  TtyInputType type;
  union {
    Unicode val_text;
  };
} TtyInputToken;

/**
 * Read a single Tty input token.
 * Returns the remaining input.
 * The token is written to the output pointer.
 */
String tty_input_lex(String, TtyInputToken*);

/**
 * Write a ANSI escape sequence to the provided dynamic-string for setting the terminal style.
 */
void tty_write_style_sequence(DynString*, TtyStyle);

/**
 * Write a ANSI escape sequence to the provided dynamic-string for setting the terminal window
 * title.
 */
void tty_write_window_title_sequence(DynString*, String title);

/**
 * Write a ANSI escape sequence to the provided dynamic-string for setting the cursor position.
 * NOTE: The values are 1-based.
 */
void tty_write_set_cursor_sequence(DynString*, u32 row, u32 col);
void tty_write_set_cursor_hor_sequence(DynString*, u32 col);

/**
 * Write a ANSI escape sequence to the provided dynamic-string for enabling / disabling the cursor.
 */
void tty_write_cursor_show_sequence(DynString*, bool show);

typedef enum {
  TtyClearMode_CursorToEnd   = 0,
  TtyClearMode_CursorToStart = 1,
  TtyClearMode_All           = 2,
} TtyClearMode;

/**
 * Write a ANSI escape sequence to the provided dynamic-string for clearing the display.
 */
void tty_write_clear_sequence(DynString*, TtyClearMode);

/**
 * Write a ANSI escape sequence to the provided dynamic-string for clearing the line.
 */
void tty_write_clear_line_sequence(DynString*, TtyClearMode);

/**
 * Write a ANSI escape sequence to the provided dynamic-string for enabling alt screen buffer.
 */
void tty_write_alt_screen_sequence(DynString*, bool enable);

/**
 * Write a ANSI escape sequence to the provided dynamic-string for enabling / disabling line wrap.
 */
void tty_write_line_wrap_sequence(DynString*, bool enable);
