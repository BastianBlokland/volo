#include "core_diag.h"
#include "core_dynarray.h"
#include "core_string.h"
#include "core_types.h"

#include "cli_parse.h"

#include "app_internal.h"

typedef struct {
  bool     provided;
  DynArray values; // String[]
} CliInvocationOption;

struct sCliInvocation {
  Allocator* alloc;
  DynArray   errors;  // String[]
  DynArray   options; // CliInvocationOption[]
};

typedef struct {
  const CliApp* app;
  Allocator*    alloc;
  bool          acceptFlags;
  u16           nextPositional;
  const char**  argHead;
  const char**  argTail;
  DynArray      errors;  // String[]
  DynArray      options; // CliInvocationOption[]
} CliParseCtx;

static CliInvocationOption* cli_invocation_option(CliInvocation* invoc, const CliId id) {
  return dynarray_at_t(&invoc->options, id, CliInvocationOption);
}

static void cli_parse_add_error(CliParseCtx* ctx, String err) {
  *dynarray_push_t(&ctx->errors, String) = string_dup(ctx->alloc, err);
}

static String cli_parse_peek_arg(CliParseCtx* ctx) {
  return ctx->argHead == ctx->argTail ? string_empty : string_from_null_term(*ctx->argHead);
}

static void cli_parse_consume_arg(CliParseCtx* ctx) {
  diag_assert(ctx->argHead != ctx->argTail);
  ++ctx->argHead;
}

static u32 cli_parse_args_remaining(CliParseCtx* ctx) { return ctx->argTail - ctx->argHead; }

static bool cli_parse_already_provided(CliParseCtx* ctx, CliId id) {
  return dynarray_at_t(&ctx->options, id, CliInvocationOption)->provided;
}

static void cli_parse_set_provided(CliParseCtx* ctx, CliId id) {
  dynarray_at_t(&ctx->options, id, CliInvocationOption)->provided = true;
}

static void
cli_parse_add_value(CliParseCtx* ctx, const CliId id, const CliOptionFlags flags, String value) {
  CliInvocationOption* opt = dynarray_at_t(&ctx->options, id, CliInvocationOption);

  /**
   * For 'multiValue' options we split on comma's.
   * This supports passing multiple values as a single string.
   * For example: '--some-option valueA,valueB,valueC --another-option'.
   */

  if ((flags & CliOptionFlags_MultiValue) == CliOptionFlags_MultiValue) {
    usize commaPos;
    while (!sentinel_check(commaPos = string_find_first(value, string_lit(",")))) {
      const String partBeforeComma = string_slice(value, 0, commaPos);
      if (LIKELY(!string_is_empty(partBeforeComma))) {
        *dynarray_push_t(&opt->values, String) = partBeforeComma;
      }
      value = string_consume(value, commaPos + 1);
    }
  }

  if (LIKELY(!string_is_empty(value))) {
    *dynarray_push_t(&opt->values, String) = value;
  }
}

static void cli_parse_add_values(CliParseCtx* ctx, CliId optId) {
  const CliOptionFlags flags = cli_option(ctx->app, optId)->flags;
  if (!(flags & CliOptionFlags_Value)) {
    return; // Option does not support passing values.
  }

  // Skip any empty arguments.
  while (cli_parse_args_remaining(ctx) && string_is_empty(cli_parse_peek_arg(ctx))) {
    cli_parse_consume_arg(ctx);
  }

  if (!cli_parse_args_remaining(ctx)) {
    cli_parse_add_error(
        ctx,
        fmt_write_scratch(
            "Value missing for option '{}'", fmt_text(cli_option_name(ctx->app, optId))));
    return;
  }

  // Consume the next argument.
  cli_parse_add_value(ctx, optId, flags, cli_parse_peek_arg(ctx));
  cli_parse_consume_arg(ctx);

  /**
   * For 'multiValue' options we keep consuming args until the next flag is found.
   * This supports passing multiple values as separate strings.
   * For example: '--some-option valueA valueB valueC --another-option'.
   */

  const bool multiValue = (flags & CliOptionFlags_MultiValue) == CliOptionFlags_MultiValue;
  while (multiValue && cli_parse_args_remaining(ctx)) {
    String head = cli_parse_peek_arg(ctx);
    if (string_is_empty(head)) {
      cli_parse_consume_arg(ctx);
      continue;
    }
    if (ctx->acceptFlags && string_starts_with(head, string_lit("-"))) {
      // Stop when a flag is encountered.
      break;
    }
    cli_parse_add_value(ctx, optId, flags, cli_parse_peek_arg(ctx));
    cli_parse_consume_arg(ctx);
  }
}

static void cli_parse_long_flag(CliParseCtx* ctx, String name) {
  const CliId optId = cli_find_by_name(ctx->app, name);
  if (sentinel_check(optId)) {
    cli_parse_add_error(ctx, fmt_write_scratch("Unknown flag '{}'", fmt_text(name)));
    return;
  }
  if (cli_parse_already_provided(ctx, optId)) {
    cli_parse_add_error(ctx, fmt_write_scratch("Duplicate flag '{}'", fmt_text(name)));
    return;
  }
  cli_parse_set_provided(ctx, optId);
  cli_parse_add_values(ctx, optId);
}

static void cli_parse_short_flag(CliParseCtx* ctx, u8 character) {
  const CliId optId = cli_find_by_character(ctx->app, character);
  if (sentinel_check(optId)) {
    cli_parse_add_error(ctx, fmt_write_scratch("Unknown flag '{}'", fmt_char(character)));
    return;
  }
  if (cli_parse_already_provided(ctx, optId)) {
    cli_parse_add_error(ctx, fmt_write_scratch("Duplicate flag '{}'", fmt_char(character)));
    return;
  }
  cli_parse_set_provided(ctx, optId);
  cli_parse_add_values(ctx, optId);
}

static void cli_parse_short_flag_block(CliParseCtx* ctx, String characterBlock) {
  mem_for_u8(characterBlock, character, {
    const CliId optId = cli_find_by_character(ctx->app, character);
    if (sentinel_check(optId)) {
      cli_parse_add_error(ctx, fmt_write_scratch("Unknown flag '{}'", fmt_char(character)));
      continue;
    }
    if (cli_parse_already_provided(ctx, optId)) {
      cli_parse_add_error(ctx, fmt_write_scratch("Duplicate flag '{}'", fmt_char(character)));
      continue;
    }
    if (cli_option(ctx->app, optId)->flags & CliOptionFlags_Value) {
      cli_parse_add_error(ctx, fmt_write_scratch("Flag '{}' takes a value", fmt_char(character)));
      continue;
    }
    cli_parse_set_provided(ctx, optId);
  });
}

static void cli_parse_arg(CliParseCtx* ctx) {
  const CliId optId = cli_find_by_position(ctx->app, ctx->nextPositional);
  if (sentinel_check(optId)) {
    cli_parse_add_error(
        ctx, fmt_write_scratch("Invalid input '{}'", fmt_text(cli_parse_peek_arg(ctx))));
    cli_parse_consume_arg(ctx);
    return;
  }
  ++ctx->nextPositional;
  cli_parse_set_provided(ctx, optId);
  cli_parse_add_values(ctx, optId);
}

static void cli_parse_options(CliParseCtx* ctx) {
  while (cli_parse_args_remaining(ctx)) {
    const String head = cli_parse_peek_arg(ctx);

    if (string_is_empty(head)) {
      // Ignore empty arguments.
      cli_parse_consume_arg(ctx);
      continue;
    }

    if (ctx->acceptFlags && string_eq(head, string_lit("--"))) {
      cli_parse_consume_arg(ctx);
      ctx->acceptFlags = false;
      continue;
    }

    if (ctx->acceptFlags && string_eq(head, string_lit("-"))) {
      // Single dash is ignored, usefull as a seperator for list arguments.
      cli_parse_consume_arg(ctx);
      continue;
    }

    if (ctx->acceptFlags && string_starts_with(head, string_lit("--"))) {
      cli_parse_consume_arg(ctx);
      cli_parse_long_flag(ctx, string_consume(head, 2));
      continue;
    }

    if (ctx->acceptFlags && string_starts_with(head, string_lit("-"))) {
      if (head.size == 2) {
        cli_parse_consume_arg(ctx);
        cli_parse_short_flag(ctx, *string_at(head, 1));
      } else {
        cli_parse_consume_arg(ctx);
        cli_parse_short_flag_block(ctx, string_consume(head, 1));
      }
      continue;
    }

    cli_parse_arg(ctx);
  }
}

static void cli_parse_check_validator(CliParseCtx* ctx, const CliId optId) {

  CliInvocationOption* invocOpt = dynarray_at_t(&ctx->options, optId, CliInvocationOption);
  dynarray_for_t(&invocOpt->values, String, val, {
    if (!cli_option(ctx->app, optId)->validator(*val)) {
      const String err = fmt_write_scratch(
          "Invalid input '{}' for option '{}'",
          fmt_text(*val, .flags = FormatTextFlags_EscapeNonPrintAscii),
          fmt_text(cli_option_name(ctx->app, optId)));
      cli_parse_add_error(ctx, err);
    }
  });
}

static void cli_parse_check_validators(CliParseCtx* ctx) {
  for (CliId optId = 0; optId != ctx->options.size; ++optId) {
    if (cli_option(ctx->app, optId)->validator) {
      cli_parse_check_validator(ctx, optId);
    }
  }
}

static void cli_parse_check_exclusions(CliParseCtx* ctx) {
  dynarray_for_t((DynArray*)&ctx->app->exclusions, CliExclusion, ex, {
    if (cli_parse_already_provided(ctx, ex->a) && cli_parse_already_provided(ctx, ex->b)) {
      cli_parse_add_error(
          ctx,
          fmt_write_scratch(
              "Options '{}' and '{}' cannot be used together",
              fmt_text(cli_option_name(ctx->app, ex->a)),
              fmt_text(cli_option_name(ctx->app, ex->b))));
    }
  });
}

static void cli_parse_check_required_option(CliParseCtx* ctx, const CliId optId) {
  CliOption* opt        = cli_option(ctx->app, optId);
  const bool isRequired = (opt->flags & CliOptionFlags_Required) == CliOptionFlags_Required;
  if (!isRequired || cli_parse_already_provided(ctx, optId)) {
    return; // Option was not required or was actually provided; Success.
  }

  /**
   * Option was not provided, check if an exclusion option was provided.
   * This supports two mutually exclusive required options (meaning either one has to be provided).
   */
  dynarray_for_t((DynArray*)&ctx->app->exclusions, CliExclusion, ex, {
    if (ex->a == optId && cli_parse_already_provided(ctx, ex->b)) {
      return; // Alternative option was provided; Success.
    }
    if (ex->b == optId && cli_parse_already_provided(ctx, ex->a)) {
      return; // Alternative option was provided; Success.
    }
  });
  cli_parse_add_error(
      ctx,
      fmt_write_scratch(
          "Required option '{}' was not provided", fmt_text(cli_option_name(ctx->app, optId))));
}

static void cli_parse_check_required_options(CliParseCtx* ctx) {
  for (CliId optId = 0; optId != ctx->options.size; ++optId) {
    cli_parse_check_required_option(ctx, optId);
  }
}

CliInvocation* cli_parse(const CliApp* app, const int argc, const char** argv) {
  diag_assert_msg(argc >= 0, "'argc' (argument count) with a negative value is not supported");

  CliParseCtx ctx = {
      .app            = app,
      .alloc          = app->alloc,
      .acceptFlags    = true,
      .nextPositional = 0,
      .argHead        = argv,
      .argTail        = argv + argc,
      .errors         = dynarray_create_t(app->alloc, String, 0),
      .options        = dynarray_create_t(app->alloc, CliInvocationOption, app->options.size),
  };
  // Initialize all options to a default state.
  for (u32 i = 0; i != app->options.size; ++i) {
    *dynarray_push_t(&ctx.options, CliInvocationOption) = (CliInvocationOption){
        .provided = false,
        .values   = dynarray_create_t(app->alloc, String, 0),
    };
  }

  cli_parse_options(&ctx);
  cli_parse_check_validators(&ctx);
  cli_parse_check_exclusions(&ctx);
  cli_parse_check_required_options(&ctx);

  CliInvocation* invoc = alloc_alloc_t(app->alloc, CliInvocation);
  *invoc               = (CliInvocation){
      .alloc   = app->alloc,
      .errors  = ctx.errors,
      .options = ctx.options,
  };
  return invoc;
}

void cli_parse_destroy(CliInvocation* invoc) {
  dynarray_for_t(&invoc->errors, String, err, { string_free(invoc->alloc, *err); });
  dynarray_destroy(&invoc->errors);

  dynarray_for_t(&invoc->options, CliInvocationOption, opt, { dynarray_destroy(&opt->values); });
  dynarray_destroy(&invoc->options);

  alloc_free_t(invoc->alloc, invoc);
}

CliParseResult cli_parse_result(const CliInvocation* invoc) {
  return invoc->errors.size ? CliParseResult_Fail : CliParseResult_Success;
}

CliParseErrors cli_parse_errors(const CliInvocation* invoc) {
  return (CliParseErrors){
      .head  = invoc->errors.size ? dynarray_at_t(&invoc->errors, 0, String) : null,
      .count = invoc->errors.size,
  };
}

bool cli_parse_provided(const CliInvocation* invoc, const CliId id) {
  return cli_invocation_option((CliInvocation*)invoc, id)->provided;
}

CliParseValues cli_parse_values(const CliInvocation* invoc, const CliId id) {
  const CliInvocationOption* opt = cli_invocation_option((CliInvocation*)invoc, id);
  return (CliParseValues){
      .head  = opt->values.size ? dynarray_at_t(&opt->values, 0, String) : null,
      .count = opt->values.size,
  };
}
