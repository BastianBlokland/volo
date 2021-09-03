#include "cli_app.h"
#include "cli_parse.h"
#include "cli_read.h"

#include "check_spec.h"

spec(read) {

  it("returns the provided string") {
    CliApp*     app  = cli_app_create(g_alloc_heap, string_lit("My test app"));
    const CliId flag = cli_register_flag(app, 's', string_lit("string"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"-s", "Hello World"});

    check_eq_string(cli_read_string(invoc, flag, string_lit("Backup")), string_lit("Hello World"));

    cli_parse_destroy(invoc);
    cli_app_destroy(app);
  }

  it("returns the default when not providing a string") {
    CliApp*     app  = cli_app_create(g_alloc_heap, string_lit("My test app"));
    const CliId flag = cli_register_flag(app, 's', string_lit("string"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 0, null);

    check_eq_string(cli_read_string(invoc, flag, string_lit("Goodbye")), string_lit("Goodbye"));

    cli_parse_destroy(invoc);
    cli_app_destroy(app);
  }

  it("returns the provided i64") {
    CliApp*     app  = cli_app_create(g_alloc_heap, string_lit("My test app"));
    const CliId flag = cli_register_flag(app, 'i', string_lit("int"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"-i", "-42"});

    check_eq_i64(cli_read_i64(invoc, flag, -1), -42);

    cli_parse_destroy(invoc);
    cli_app_destroy(app);
  }

  it("returns the default when not providing a i64") {
    CliApp*     app  = cli_app_create(g_alloc_heap, string_lit("My test app"));
    const CliId flag = cli_register_flag(app, 'i', string_lit("int"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 0, null);

    check_eq_i64(cli_read_i64(invoc, flag, -1), -1);

    cli_parse_destroy(invoc);
    cli_app_destroy(app);
  }

  it("returns the provided u64") {
    CliApp*     app  = cli_app_create(g_alloc_heap, string_lit("My test app"));
    const CliId flag = cli_register_flag(app, 'i', string_lit("int"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"-i", "42"});

    check_eq_u64(cli_read_u64(invoc, flag, 999), 42);

    cli_parse_destroy(invoc);
    cli_app_destroy(app);
  }

  it("returns the default when not providing a u64") {
    CliApp*     app  = cli_app_create(g_alloc_heap, string_lit("My test app"));
    const CliId flag = cli_register_flag(app, 'i', string_lit("int"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 0, null);

    check_eq_u64(cli_read_u64(invoc, flag, 999), 999);

    cli_parse_destroy(invoc);
    cli_app_destroy(app);
  }

  it("returns the provided f64") {
    CliApp*     app  = cli_app_create(g_alloc_heap, string_lit("My test app"));
    const CliId flag = cli_register_flag(app, 'f', string_lit("float"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"-f", "42.1337e-2"});

    check_eq_f64(cli_read_f64(invoc, flag, 999.999), 42.1337e-2, 1e-32);

    cli_parse_destroy(invoc);
    cli_app_destroy(app);
  }
}
