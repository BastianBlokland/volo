#include "check/spec.h"
#include "cli/app.h"
#include "cli/failure.h"
#include "cli/parse.h"
#include "core/alloc.h"
#include "core/dynstring.h"

spec(failure) {

  it("can write a failure page") {
    CliApp*        app   = cli_app_create(g_allocHeap);
    CliInvocation* invoc = cli_parse_lit(app, string_lit("Hello"), string_lit("World"));

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
