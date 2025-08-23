#include "cli/help.h"
#include "core/alloc.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/format.h"
#include "core/tty.h"
#include "core/version.h"

#include "app.h"

#define cli_help_max_width 80

static FormatArg arg_style_bold(const CliHelpFlags flags) {
  return flags & CliHelpFlags_Style ? fmt_ttystyle(.flags = TtyStyleFlags_Bold) : fmt_nop();
}

static FormatArg arg_style_reset(const CliHelpFlags flags) {
  return flags & CliHelpFlags_Style ? fmt_ttystyle() : fmt_nop();
}

static bool cli_help_has_options_of_type(const CliApp* app, const CliOptionType type) {
  dynarray_for_t(&app->options, CliOption, opt) {
    if (opt->type == type) {
      return true;
    }
  }
  return false;
}

static String cli_help_option_usage(CliOption* opt) {
  DynString dynStr = dynstring_create_over(alloc_alloc(g_allocScratch, 128, 1));

  const bool optional   = (opt->flags & CliOptionFlags_Required) != CliOptionFlags_Required;
  const bool value      = (opt->flags & CliOptionFlags_Value) == CliOptionFlags_Value;
  const bool multiValue = (opt->flags & CliOptionFlags_MultiValue) == CliOptionFlags_MultiValue;

  if (optional) {
    fmt_write(&dynStr, "[");
  }

  switch (opt->type) {
  case CliOptionType_Flag:
    if (opt->dataFlag.character) {
      fmt_write(&dynStr, "-{}", fmt_char(opt->dataFlag.character));
    } else {
      fmt_write(&dynStr, "--{}", fmt_text(opt->dataFlag.name));
    }
    if (value) {
      fmt_write(&dynStr, " <value{}>", multiValue ? fmt_text_lit("...") : fmt_nop());
    }
    break;
  case CliOptionType_Arg:
    fmt_write(
        &dynStr,
        "<{}{}>",
        fmt_text(opt->dataArg.name),
        multiValue ? fmt_text_lit("...") : fmt_nop());
    break;
  }
  if (optional) {
    fmt_write(&dynStr, "]");
  }

  String res = dynstring_view(&dynStr);
  dynstring_destroy(&dynStr);
  return res;
}

static void cli_help_write_usage(DynString* dynStr, const CliApp* app, const CliHelpFlags flags) {
  fmt_write(
      dynStr, "usage: {}{}{}", arg_style_bold(flags), fmt_text(app->name), arg_style_reset(flags));

  const usize startColumn = string_lit("usage: ").size + app->name.size;
  usize       column      = startColumn;

  dynarray_for_t(&app->options, CliOption, opt) {
    if (opt->flags & CliOptionFlags_Exclusive) {
      continue;
    }
    const String optStr = cli_help_option_usage(opt);
    if ((column + optStr.size + 1) > cli_help_max_width) {
      column = startColumn;
      fmt_write(dynStr, "\n{}", fmt_padding((u16)startColumn));
    }
    fmt_write(dynStr, " {}", fmt_text(optStr));
    column += optStr.size + 1;
  }

  fmt_write(dynStr, "\n");
}

static void cli_help_write_args(DynString* dynStr, const CliApp* app, const CliHelpFlags flags) {
  fmt_write(dynStr, "{}Arguments:{}\n", arg_style_bold(flags), arg_style_reset(flags));

  dynarray_for_t(&app->options, CliOption, opt) {
    if (opt->type != CliOptionType_Arg) {
      continue;
    }

    const bool required = (opt->flags & CliOptionFlags_Required) == CliOptionFlags_Required;

    const String line = fmt_write_scratch(
        " {<25}{<10}",
        fmt_text(opt->dataArg.name),
        required ? fmt_text_lit("REQUIRED") : fmt_text_lit("OPTIONAL"));

    dynstring_append(dynStr, line);

    const String linePrefix = format_write_arg_scratch(&fmt_padding((u16)line.size));
    format_write_text_wrapped(dynStr, opt->desc, cli_help_max_width - linePrefix.size, linePrefix);

    fmt_write(dynStr, "\n");
  }
}

static void cli_help_write_flags(DynString* dynStr, const CliApp* app, const CliHelpFlags flags) {
  fmt_write(dynStr, "{}Flags:{}\n", arg_style_bold(flags), arg_style_reset(flags));

  dynarray_for_t(&app->options, CliOption, opt) {
    if (opt->type != CliOptionType_Flag) {
      continue;
    }

    const bool required = (opt->flags & CliOptionFlags_Required) == CliOptionFlags_Required;

    const String shortName = opt->dataFlag.character
                                 ? fmt_write_scratch("-{},", fmt_char(opt->dataFlag.character))
                                 : string_empty;
    const String longName  = fmt_write_scratch("--{}", fmt_text(opt->dataFlag.name));

    const String line = fmt_write_scratch(
        " {<4}{<21}{<10}",
        fmt_text(shortName),
        fmt_text(longName),
        required ? fmt_text_lit("REQUIRED") : fmt_text_lit("OPTIONAL"));

    dynstring_append(dynStr, line);

    const String linePrefix = format_write_arg_scratch(&fmt_padding((u16)line.size));
    format_write_text_wrapped(dynStr, opt->desc, cli_help_max_width - line.size, linePrefix);

    fmt_write(dynStr, "\n");
  }
}

static void cli_help_write_version(DynString* dynStr, const CliHelpFlags flags) {
  fmt_write(dynStr, "{}Version:{} ", arg_style_bold(flags), arg_style_reset(flags));
  version_str(g_versionExecutable, dynStr);
  fmt_write(dynStr, "\n");
}

void cli_help_write(DynString* dynStr, const CliApp* app, const CliHelpFlags flags) {

  cli_help_write_usage(dynStr, app, flags);

  if (!string_is_empty(app->desc)) {
    fmt_write(dynStr, "\n");
    format_write_text_wrapped(dynStr, app->desc, cli_help_max_width, string_empty);
    fmt_write(dynStr, "\n");
  }

  if (cli_help_has_options_of_type(app, CliOptionType_Arg)) {
    fmt_write(dynStr, "\n");
    cli_help_write_args(dynStr, app, flags);
  }

  if (cli_help_has_options_of_type(app, CliOptionType_Flag)) {
    fmt_write(dynStr, "\n");
    cli_help_write_flags(dynStr, app, flags);
  }

  if (flags & CliHelpFlags_IncludeVersion) {
    fmt_write(dynStr, "\n");
    cli_help_write_version(dynStr, flags);
  }
}

void cli_help_write_file(const CliApp* app, CliHelpFlags flags, File* out) {
  DynString str = dynstring_create(g_allocHeap, 1024);

  if (tty_isatty(out)) {
    flags |= CliHelpFlags_Style;
  } else {
    flags &= ~CliHelpFlags_Style;
  }
  cli_help_write(&str, app, flags);

  file_write_sync(out, dynstring_view(&str));
  dynstring_destroy(&str);
}
