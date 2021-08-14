#include "core_diag.h"

#include "cli_app.h"

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
      string_free(app->alloc, opt->dataFlag.longName);
      break;
    case CliOptionType_Arg:
      break;
    }
    string_free(app->alloc, opt->desc);
  });
  dynarray_destroy(&app->options);

  alloc_free_t(app->alloc, app);
}

CliId cli_register_flag(
    CliApp*              app,
    const u8             shortName,
    const String         longName,
    const String         desc,
    const CliOptionFlags flags) {

  diag_assert_msg(longName.size, "Flag needs a long name");
  diag_assert_msg(desc.size, "Flag needs a description");

  const CliId id = (CliId)app->options.size;

  *dynarray_push_t(&app->options, CliOption) = (CliOption){
      .type     = CliOptionType_Flag,
      .desc     = string_dup(app->alloc, desc),
      .flags    = flags,
      .dataFlag = {
          .shortName = shortName,
          .longName  = string_dup(app->alloc, longName),
      }};
  return id;
}

CliId cli_register_arg(CliApp* app, const String desc, const CliOptionFlags flags) {

  diag_assert_msg(desc.size, "Arg needs a description");

  u16 position = 0;
  dynarray_for_t(&app->options, CliOption, opt, {
    if (opt->type == CliOptionType_Arg) {
      ++position;
    }
  });

  const CliId id = (CliId)app->options.size;

  *dynarray_push_t(&app->options, CliOption) = (CliOption){
      .type    = CliOptionType_Arg,
      .desc    = string_dup(app->alloc, desc),
      .flags   = flags,
      .dataArg = {.position = position},
  };
  return id;
}
