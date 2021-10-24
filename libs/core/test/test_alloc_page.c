#include "check_spec.h"
#include "core_alloc.h"

spec(alloc_page) {

  it("ensures alignment of allocation matches page-size") {
    const usize pageSize = 4096;

    Mem alloc = alloc_alloc(g_alloc_page, 8, 2);
    check_eq_int(((uptr)alloc.ptr & (pageSize - 1)), 0);

    alloc_free(g_alloc_page, alloc);
  }

  it("can allocate memory smaller then the page-size") {
    const Mem alloc = alloc_alloc(g_alloc_page, 64, 8);

    check_eq_int(alloc.size, 64);

    alloc_free(g_alloc_page, alloc);
  }
}
