#include "core_dynstring.h"
#include "core_format.h"
#include "core_tty.h"

#include "cli_failure.h"
#include "cli_parse.h"

static FormatArg arg_style_red_bg(const CliFailureFlags flags) {
  return flags & CliFailureFlags_Style
             ? fmt_ttystyle(.bgColor = TtyBgColor_Red, .flags = TtyStyleFlags_Bold)
             : fmt_nop();
}

static FormatArg arg_style_reset(const CliFailureFlags flags) {
  return flags & CliFailureFlags_Style ? fmt_ttystyle() : fmt_nop();
}

void cli_failure_write(DynString* dynStr, CliInvocation* invoc, const CliFailureFlags flags) {
  CliParseErrors errors = cli_parse_errors(invoc);
  for (String* err = errors.head; err != errors.head + errors.count; ++err) {
    fmt_write(dynStr, "{}{}{}\n", arg_style_red_bg(flags), fmt_text(*err), arg_style_reset(flags));
  }
}

void cli_failure_write_file(CliInvocation* invoc, File* out) {
  DynString str = dynstring_create(g_alloc_heap, 512);

  const CliFailureFlags flags = tty_isatty(out) ? CliFailureFlags_Style : CliFailureFlags_None;
  cli_failure_write(&str, invoc, flags);

  file_write_sync(out, dynstring_view(&str));
  dynstring_destroy(&str);
}
