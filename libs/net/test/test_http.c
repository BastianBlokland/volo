#include "check_spec.h"
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_types.h"
#include "net_http.h"
#include "net_result.h"

spec(http) {
  skip_it("can get a resource") {
    const String host = string_lit("bastian.tech");
    const String uri  = string_lit("/test/hello-world.txt");
    NetHttp*     http = net_http_connect_sync(g_allocHeap, host, NetHttpFlags_TlsNoVerify);
    check_eq_int(net_http_status(http), NetResult_Success);

    DynString data = dynstring_create(g_allocHeap, usize_kibibyte);
    check_eq_int(net_http_get_sync(http, uri, &data), NetResult_Success);
    check_eq_string(dynstring_view(&data), string_lit("Hello World!\n"));
    dynstring_destroy(&data);

    check_eq_int(net_http_shutdown_sync(http), NetResult_Success);

    net_http_destroy(http);
  }
}
