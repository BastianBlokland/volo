#include "check/spec.h"
#include "core/alloc.h"

spec(alloc_page) {

  it("ensures alignment of allocation matches page-size") {
    const usize pageSize = 4096;

    Mem alloc = alloc_alloc(g_allocPage, 8, 2);
    check_eq_int(((uptr)alloc.ptr & (pageSize - 1)), 0);

    alloc_free(g_allocPage, alloc);
  }

  it("can allocate memory smaller then the page-size") {
    const Mem alloc = alloc_alloc(g_allocPage, 64, 8);

    check_eq_int(alloc.size, 64);

    alloc_free(g_allocPage, alloc);
  }
}
