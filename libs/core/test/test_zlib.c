#include "check_spec.h"
#include "core_alloc.h"
#include "core_base64.h"
#include "core_dynstring.h"
#include "core_zlib.h"

spec(zlib) {

  it("can decode a hello-world file") {
    const String dataB64 = string_lit("eF7zSM3JyVcIzy/KSVHkAgAgkQRI");
    const String data    = base64_decode_scratch(dataB64);

    Mem       outputMem    = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
    DynString outputBuffer = dynstring_create_over(outputMem);

    ZlibError    err;
    const String remaining = zlib_decode(data, &outputBuffer, &err);

    check_eq_int(err, ZlibError_None);
    check_eq_string(remaining, string_empty);
    check_eq_string(dynstring_view(&outputBuffer), string_lit("Hello World!\n"));
  }
}
