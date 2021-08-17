#include "cli_app.h"
#include "cli_failure.h"
#include "cli_parse.h"

#include "check_spec.h"

spec(failure) {

  it("can write a failure page") {
    CliApp*        app   = cli_app_create(g_alloc_heap, string_empty);
    CliInvocation* invoc = cli_parse(app, 2, (const char*[]){"Hello", "World"});

    DynString string = dynstring_create_over(mem_stack(1024));
    cli_failure_write(&string, invoc, CliFailureFlags_None);

    check_eq_string(
        dynstring_view(&string),
        string_lit("Invalid input 'Hello'\n"
                   "Invalid input 'World'\n"));

    dynstring_destroy(&string);

    cli_parse_destroy(invoc);
    cli_app_destroy(app);
  }
}
