#include "core_diag.h"

#include "app_internal.h"

CliApp* cli_app_create(Allocator* alloc, const String desc) {
  CliApp* app = alloc_alloc_t(alloc, CliApp);
  *app        = (CliApp){
      .desc    = string_dup(alloc, desc),
      .options = dynarray_create_t(alloc, CliOption, 16),
      .alloc   = alloc,
  };
  return app;
}

void cli_app_destroy(CliApp* app) {
  string_free(app->alloc, app->desc);

  dynarray_for_t(&app->options, CliOption, opt, {
    switch (opt->type) {
    case CliOptionType_Flag:
      string_free(app->alloc, opt->dataFlag.name);
      break;
    case CliOptionType_Arg:
      string_free(app->alloc, opt->dataArg.name);
      break;
    }
  });
  dynarray_destroy(&app->options);

  alloc_free_t(app->alloc, app);
}

CliId cli_register_flag(
    CliApp* app, const u8 character, const String name, const CliOptionFlags flags) {

  diag_assert_msg(!string_is_empty(name), "Flag needs a name");

  diag_assert_msg(
      character == '\0' || sentinel_check(cli_find_by_character(app, character)),
      "Duplicate flag with character '{}' ",
      fmt_char(character, .flags = FormatTextFlags_EscapeNonPrintAscii));

  diag_assert_msg(
      sentinel_check(cli_find_by_name(app, name)), "Duplicate flag with name '{}'", fmt_text(name));

  const CliId id = (CliId)app->options.size;

  *dynarray_push_t(&app->options, CliOption) = (CliOption){
      .type     = CliOptionType_Flag,
      .flags    = flags,
      .dataFlag = {
          .character = character,
          .name      = string_dup(app->alloc, name),
      }};
  return id;
}

CliId cli_register_arg(CliApp* app, const String name, const CliOptionFlags flags) {
  diag_assert_msg(!string_is_empty(name), "Argument needs a name");

  u16 position = 0;
  dynarray_for_t(&app->options, CliOption, opt, {
    if (opt->type == CliOptionType_Arg) {
      ++position;
    }
  });

  const CliId id = (CliId)app->options.size;

  *dynarray_push_t(&app->options, CliOption) = (CliOption){
      .type    = CliOptionType_Arg,
      .flags   = flags | CliOptionFlags_Value,
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
  diag_assert_fail("Unsupported option type");
}

CliId cli_find_by_character(const CliApp* app, const u8 character) {
  diag_assert_msg(character, "Null is not a valid flag character");

  dynarray_for_t((DynArray*)&app->options, CliOption, opt, {
    if (opt->type == CliOptionType_Flag && opt->dataFlag.character == character) {
      return (CliId)opt_i;
    }
  });
  return sentinel_u16;
}

CliId cli_find_by_name(const CliApp* app, const String name) {
  diag_assert_msg(!string_is_empty(name), "Empty string is not a valid flag name");

  dynarray_for_t((DynArray*)&app->options, CliOption, opt, {
    if (opt->type == CliOptionType_Flag && string_eq(opt->dataFlag.name, name)) {
      return (CliId)opt_i;
    }
  });
  return sentinel_u16;
}

CliId cli_find_by_position(const CliApp* app, const u16 position) {
  dynarray_for_t((DynArray*)&app->options, CliOption, opt, {
    if (opt->type == CliOptionType_Arg && opt->dataArg.position == position) {
      return (CliId)opt_i;
    }
  });
  return sentinel_u16;
}
