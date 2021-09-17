#include "check_spec.h"
#include "cli_app.h"
#include "cli_help.h"

spec(help) {

  it("can write a help page for an empty app") {
    CliApp* app = cli_app_create(g_alloc_heap, string_empty);

    DynString string = dynstring_create_over(mem_stack(1024));
    cli_help_write(&string, app, CliHelpFlags_None);

    check_eq_string(dynstring_view(&string), string_lit("usage: volo_cli_test\n"));

    dynstring_destroy(&string);
    cli_app_destroy(app);
  }

  it("can write a help page for an app with a description") {
    CliApp* app = cli_app_create(g_alloc_heap, string_lit("Hello world\nMy test app"));

    DynString string = dynstring_create_over(mem_stack(1024));
    cli_help_write(&string, app, CliHelpFlags_None);

    check_eq_string(
        dynstring_view(&string),
        string_lit("usage: volo_cli_test\n"
                   "\n"
                   "Hello world\n"
                   "My test app\n"));

    dynstring_destroy(&string);
    cli_app_destroy(app);
  }

  it("can write a help page for an app with arguments") {
    CliApp*     app = cli_app_create(g_alloc_heap, string_empty);
    const CliId src = cli_register_arg(app, string_lit("src-path"), CliOptionFlags_Required);
    const CliId dst = cli_register_arg(app, string_lit("dst-path"), CliOptionFlags_None);

    cli_register_desc(app, src, string_lit("Path to copy from"));
    cli_register_desc(app, dst, string_lit("Path to copy to"));

    DynString string = dynstring_create_over(mem_stack(1024));
    cli_help_write(&string, app, CliHelpFlags_None);

    check_eq_string(
        dynstring_view(&string),
        string_lit("usage: volo_cli_test <src-path> [<dst-path>]\n"
                   "\n"
                   "Arguments:\n"
                   " src-path                 REQUIRED  Path to copy from\n"
                   " dst-path                 OPTIONAL  Path to copy to\n"));

    dynstring_destroy(&string);
    cli_app_destroy(app);
  }

  it("can write a help page for an app with flags") {
    CliApp*     app     = cli_app_create(g_alloc_heap, string_empty);
    const CliId verbose = cli_register_flag(app, 'v', string_lit("verbose"), CliOptionFlags_None);
    const CliId count   = cli_register_flag(app, 'c', string_lit("count"), CliOptionFlags_Value);

    cli_register_desc(app, verbose, string_lit("Enable verbose logging"));
    cli_register_desc(app, count, string_lit("How many interations to run"));

    DynString string = dynstring_create_over(mem_stack(1024));
    cli_help_write(&string, app, CliHelpFlags_None);

    check_eq_string(
        dynstring_view(&string),
        string_lit("usage: volo_cli_test [-v] [-c <value>]\n"
                   "\n"
                   "Flags:\n"
                   " -v, --verbose            OPTIONAL  Enable verbose logging\n"
                   " -c, --count              OPTIONAL  How many interations to run\n"));

    dynstring_destroy(&string);
    cli_app_destroy(app);
  }

  it("can write a help page for an app with an descriptions, arguments and flags") {
    CliApp*     app     = cli_app_create(g_alloc_heap, string_lit("My app"));
    const CliId verbose = cli_register_flag(app, 'v', string_lit("verbose"), CliOptionFlags_None);
    const CliId count   = cli_register_flag(app, 'c', string_lit("count"), CliOptionFlags_Value);
    const CliId src     = cli_register_arg(app, string_lit("src-path"), CliOptionFlags_Required);
    const CliId dst     = cli_register_arg(app, string_lit("dst-path"), CliOptionFlags_None);

    cli_register_desc(app, verbose, string_lit("Enable verbose logging"));
    cli_register_desc(app, count, string_lit("How many interations to run"));
    cli_register_desc(app, src, string_lit("Path to copy from"));
    cli_register_desc(app, dst, string_lit("Path to copy to"));

    DynString string = dynstring_create_over(mem_stack(1024));
    cli_help_write(&string, app, CliHelpFlags_None);

    check_eq_string(
        dynstring_view(&string),
        string_lit("usage: volo_cli_test [-v] [-c <value>] <src-path> [<dst-path>]\n"
                   "\n"
                   "My app\n"
                   "\n"
                   "Arguments:\n"
                   " src-path                 REQUIRED  Path to copy from\n"
                   " dst-path                 OPTIONAL  Path to copy to\n"
                   "\n"
                   "Flags:\n"
                   " -v, --verbose            OPTIONAL  Enable verbose logging\n"
                   " -c, --count              OPTIONAL  How many interations to run\n"));

    dynstring_destroy(&string);
    cli_app_destroy(app);
  }
}
