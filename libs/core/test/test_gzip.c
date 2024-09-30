#include "check_spec.h"
#include "core_alloc.h"
#include "core_base64.h"
#include "core_gzip.h"

spec(gzip) {

  it("can decode a hello-world file") {
    const String dataB64 = string_lit("H4sICNne+mYAA3Rlc3QudHh0APNIzcnJVwjPL8pJUeQCAN3dFH0NAAAA");
    const String data    = base64_decode_scratch(dataB64);

    Mem       outputMem    = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
    DynString outputBuffer = dynstring_create_over(outputMem);

    GzipError    err;
    GzipMeta     meta;
    const String remaining = gzip_decode(data, &meta, &outputBuffer, &err);

    check_eq_int(err, GzipError_None);
    check_eq_string(remaining, string_empty);
    check_eq_string(meta.name, string_lit("test.txt"));
    check_eq_string(dynstring_view(&outputBuffer), string_lit("Hello World!\n"));
  }
}
