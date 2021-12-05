#include "core_alloc.h"
#include "core_ascii.h"
#include "core_diag.h"
#include "core_path.h"

#include "app_internal.h"

#define cli_app_option_name_max_len 64

CliApp* cli_app_create(Allocator* alloc, const String desc) {
  CliApp* app = alloc_alloc_t(alloc, CliApp);
  *app        = (CliApp){
      .name       = path_stem(g_path_executable),
      .desc       = string_is_empty(desc) ? string_empty : string_dup(alloc, desc),
      .options    = dynarray_create_t(alloc, CliOption, 16),
      .exclusions = dynarray_create_t(alloc, CliExclusion, 8),
      .alloc      = alloc,
  };
  return app;
}

void cli_app_destroy(CliApp* app) {
  if (!string_is_empty(app->desc)) {
    string_free(app->alloc, app->desc);
  }

  dynarray_for_t(&app->options, CliOption, opt) {
    if (opt->desc.ptr) {
      string_free(app->alloc, opt->desc);
    }
    switch (opt->type) {
    case CliOptionType_Flag:
      string_free(app->alloc, opt->dataFlag.name);
      break;
    case CliOptionType_Arg:
      string_free(app->alloc, opt->dataArg.name);
      break;
    }
  }

  dynarray_destroy(&app->options);
  dynarray_destroy(&app->exclusions);

  alloc_free_t(app->alloc, app);
}

CliId cli_register_flag(
    CliApp* app, const u8 character, const String name, const CliOptionFlags flags) {

  diag_assert_msg(!string_is_empty(name), "Flag needs a name");
  diag_assert_msg(name.size <= cli_app_option_name_max_len, "Flag name too long");

  diag_assert_msg(
      character == '\0' || ascii_is_printable(character),
      "Character '{}' is not printable ascii",
      fmt_char(character, .flags = FormatTextFlags_EscapeNonPrintAscii));

  diag_assert_msg(
      character == '\0' || sentinel_check(cli_find_by_character(app, character)),
      "Duplicate flag with character '{}'",
      fmt_char(character));

  diag_assert_msg(
      sentinel_check(cli_find_by_name(app, name)), "Duplicate flag with name '{}'", fmt_text(name));

  const CliId id = (CliId)app->options.size;

  *dynarray_push_t(&app->options, CliOption) = (CliOption){
      .type     = CliOptionType_Flag,
      .flags    = flags,
      .desc     = string_empty,
      .dataFlag = {
          .character = character,
          .name      = string_dup(app->alloc, name),
      }};
  return id;
}

CliId cli_register_arg(CliApp* app, const String name, const CliOptionFlags flags) {
  diag_assert_msg(!string_is_empty(name), "Argument needs a name");
  diag_assert_msg(name.size <= cli_app_option_name_max_len, "Argument name too long");

  u16 position = 0;
  dynarray_for_t(&app->options, CliOption, opt) {
    if (opt->type == CliOptionType_Arg) {
      ++position;
    }
  }

  const CliId id = (CliId)app->options.size;

  *dynarray_push_t(&app->options, CliOption) = (CliOption){
      .type    = CliOptionType_Arg,
      .flags   = flags | CliOptionFlags_Value,
      .desc    = string_empty,
      .dataArg = {
          .position = position,
          .name     = string_dup(app->alloc, name),
      }};
  return id;
}

void cli_register_validator(CliApp* app, const CliId id, CliValidateFunc validator) {
  CliOption* opt = cli_option(app, id);

  diag_assert_msg(
      !opt->validator,
      "Option '{}' already has a validator registered",
      fmt_text(cli_option_name(app, id)));

  diag_assert_msg(
      opt->flags & CliOptionFlags_Value,
      "Option '{}' doesn't take a value and thus cannot register a validator",
      fmt_text(cli_option_name(app, id)));

  opt->validator = validator;
}

void cli_register_exclusion(CliApp* app, const CliId a, const CliId b) {
  diag_assert_msg(
      !cli_excludes(app, a, b),
      "There is already a exclusion between '{}' and '{}'",
      fmt_text(cli_option_name(app, a)),
      fmt_text(cli_option_name(app, b)));
  diag_assert_msg(a != b, "An option cannot exclude itself");

  *dynarray_push_t(&app->exclusions, CliExclusion) = (CliExclusion){a, b};
}

void cli_register_exclusions_raw(
    CliApp* app, const CliId id, const CliId* otherIds, const usize otherCount) {
  for (usize i = 0; i != otherCount; ++i) {
    cli_register_exclusion(app, id, otherIds[i]);
  }
}

void cli_register_desc(CliApp* app, const CliId id, String desc) {
  diag_assert_msg(!string_is_empty(desc), "Empty descriptions are not supported");

  CliOption* opt = cli_option(app, id);

  diag_assert_msg(
      string_is_empty(opt->desc),
      "Option '{}' already has a description registered",
      fmt_text(cli_option_name(app, id)));

  if (opt->desc.ptr) {
    string_free(app->alloc, opt->desc);
  }
  opt->desc = string_dup(app->alloc, desc);
}

void cli_register_desc_choice(
    CliApp*       app,
    const CliId   id,
    String        desc,
    const String* choiceStrs,
    usize         choiceCount,
    usize         defaultChoice) {
  diag_assert_msg(choiceCount <= 1024, "Too many choices provided");

  DynString str = dynstring_create_over(alloc_alloc(g_alloc_scratch, usize_kibibyte, 1));
  if (!string_is_empty(desc)) {
    dynstring_append(&str, desc);
    dynstring_append_char(&str, ' ');
  }

  dynstring_append(&str, string_lit("Options: "));
  for (usize i = 0; i != choiceCount; ++i) {
    if (i) {
      dynstring_append(&str, string_lit(", "));
    }
    fmt_write(&str, "'{}'", fmt_text(choiceStrs[i]));
  }
  dynstring_append_char(&str, '.');

  if (!sentinel_check(defaultChoice)) {
    diag_assert_msg(defaultChoice < choiceCount, "Out of bound default choice");
    fmt_write(&str, " Default: '{}'.", fmt_text(choiceStrs[defaultChoice]));
  }

  cli_register_desc(app, id, dynstring_view(&str));
  dynstring_destroy(&str);
}

String cli_desc(const CliApp* app, const CliId id) { return cli_option(app, id)->desc; }

bool cli_excludes(const CliApp* app, const CliId a, const CliId b) {
  dynarray_for_t(&app->exclusions, CliExclusion, ex) {
    if ((ex->a == a && ex->b == b) || (ex->b == a && ex->a == b)) {
      return true;
    }
  }
  return false;
}

CliOption* cli_option(const CliApp* app, const CliId id) {
  diag_assert_msg(id < app->options.size, "Out of bounds CliId");
  return dynarray_at_t(&app->options, id, CliOption);
}

String cli_option_name(const CliApp* app, const CliId id) {
  const CliOption* opt = cli_option(app, id);
  switch (opt->type) {
  case CliOptionType_Flag:
    return opt->dataFlag.name;
  case CliOptionType_Arg:
    return opt->dataArg.name;
  }
  diag_crash_msg("Unsupported option type");
}

CliId cli_find_by_character(const CliApp* app, const u8 character) {
  diag_assert_msg(character, "Null is not a valid flag character");

  for (CliId id = 0; id != app->options.size; ++id) {
    CliOption* opt = dynarray_at_t(&app->options, id, CliOption);
    if (opt->type == CliOptionType_Flag && opt->dataFlag.character == character) {
      return id;
    }
  }
  return sentinel_u16;
}

CliId cli_find_by_name(const CliApp* app, const String name) {
  diag_assert_msg(!string_is_empty(name), "Empty string is not a valid flag name");

  for (CliId id = 0; id != app->options.size; ++id) {
    CliOption* opt = dynarray_at_t(&app->options, id, CliOption);
    if (opt->type == CliOptionType_Flag && string_eq(opt->dataFlag.name, name)) {
      return id;
    }
  }
  return sentinel_u16;
}

CliId cli_find_by_position(const CliApp* app, const u16 position) {
  for (CliId id = 0; id != app->options.size; ++id) {
    CliOption* opt = dynarray_at_t(&app->options, id, CliOption);
    if (opt->type == CliOptionType_Arg && opt->dataArg.position == position) {
      return id;
    }
  }
  return sentinel_u16;
}
