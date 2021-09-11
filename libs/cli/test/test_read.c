#include "cli_app.h"
#include "cli_parse.h"
#include "cli_read.h"

#include "check_spec.h"

spec(read) {

  CliApp* app = null;

  setup() { app = cli_app_create(g_alloc_heap, string_lit("My test app")); }

  it("returns the provided string") {
    const CliId flag = cli_register_flag(app, 's', string_lit("string"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"-s", "Hello World"});

    check_eq_string(cli_read_string(invoc, flag, string_lit("Backup")), string_lit("Hello World"));

    cli_parse_destroy(invoc);
  }

  it("returns the default when not providing a string") {
    const CliId flag = cli_register_flag(app, 's', string_lit("string"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 0, null);

    check_eq_string(cli_read_string(invoc, flag, string_lit("Goodbye")), string_lit("Goodbye"));

    cli_parse_destroy(invoc);
  }

  it("returns the provided i64") {
    const CliId flag = cli_register_flag(app, 'i', string_lit("int"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"-i", "-42"});

    check_eq_int(cli_read_i64(invoc, flag, -1), -42);

    cli_parse_destroy(invoc);
  }

  it("returns the default when not providing a i64") {
    const CliId flag = cli_register_flag(app, 'i', string_lit("int"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 0, null);

    check_eq_int(cli_read_i64(invoc, flag, -1), -1);

    cli_parse_destroy(invoc);
  }

  it("returns the provided u64") {
    const CliId flag = cli_register_flag(app, 'i', string_lit("int"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"-i", "42"});

    check_eq_int(cli_read_u64(invoc, flag, 999), 42);

    cli_parse_destroy(invoc);
  }

  it("returns the default when not providing a u64") {
    const CliId flag = cli_register_flag(app, 'i', string_lit("int"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 0, null);

    check_eq_int(cli_read_u64(invoc, flag, 999), 999);

    cli_parse_destroy(invoc);
  }

  it("returns the provided f64") {
    const CliId flag = cli_register_flag(app, 'f', string_lit("float"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"-f", "42.1337e-2"});

    check_eq_float(cli_read_f64(invoc, flag, 999.999), 42.1337e-2, 1e-32);

    cli_parse_destroy(invoc);
  }

  it("returns the default when not providing a f64") {
    const CliId flag = cli_register_flag(app, 'f', string_lit("float"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 0, null);

    check_eq_float(cli_read_f64(invoc, flag, 999.999), 999.999, 1e-32);

    cli_parse_destroy(invoc);
  }

  it("returns the index of the provided choice string") {
    static const String choices[] = {string_static("choiceA"), string_static("choiceB")};

    const CliId flag = cli_register_flag(app, 'c', string_lit("choice"), CliOptionFlags_Value);

    CliInvocation* invocA = cli_parse(app, 2, (const char*[]){"-c", "choiceA"});
    check_eq_int(cli_read_choice_array(invocA, flag, choices, 999), 0);
    cli_parse_destroy(invocA);

    CliInvocation* invocB = cli_parse(app, 2, (const char*[]){"-c", "choiceB"});
    check_eq_int(cli_read_choice_array(invocB, flag, choices, 999), 1);
    cli_parse_destroy(invocB);
  }

  it("returns the default when not providing a choice string") {
    static const String choices[] = {string_static("choiceA"), string_static("choiceB")};

    const CliId flag = cli_register_flag(app, 'c', string_lit("choice"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 0, null);
    check_eq_int(cli_read_choice_array(invoc, flag, choices, 999), 999);

    cli_parse_destroy(invoc);
  }

  it("returns the default when provided input doesn't match any choice string") {
    static const String choices[] = {string_static("choiceA"), string_static("choiceB")};

    const CliId flag = cli_register_flag(app, 'c', string_lit("choice"), CliOptionFlags_Value);

    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"-c", "choiceC"});
    check_eq_int(cli_read_choice_array(invoc, flag, choices, 999), 999);

    cli_parse_destroy(invoc);
  }

  teardown() { cli_app_destroy(app); }
}
