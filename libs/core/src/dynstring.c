#include "core_dynstring.h"

DynString dynstring_create(Allocator* alloc, usize capacity) {
  return dynarray_create(alloc, 1u, capacity);
}

void dynstring_destroy(DynString* dynstring) { dynarray_destroy(dynstring); }

String dynstring_view(const DynString* dynstring) {
  return dynarray_at(dynstring, 0, dynstring->size);
}

void dynstring_append(DynString* dynstring, String value) {
  mem_cpy(dynarray_push(dynstring, value.size), value);
}
