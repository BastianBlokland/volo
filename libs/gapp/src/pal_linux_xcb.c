#include "pal_internal.h"

struct sGAppPal {
  Allocator* alloc;
  //
};

GAppPal* gapp_pal_create(Allocator* alloc) {
  GAppPal* app = alloc_alloc_t(alloc, GAppPal);
  *app         = (GAppPal){
      .alloc = alloc,
      //
  };
  return app;
}

void gapp_pal_destroy(GAppPal* pal) {
  alloc_free_t(pal->alloc, pal);
  //
}
