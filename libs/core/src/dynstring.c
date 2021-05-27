#include "core_dynstring.h"

DynString dynstring_create(Allocator* alloc, usize capacity) {
  return dynarray_create(alloc, 1u, capacity);
}

void dynstring_destroy(DynString* dynstring) { dynarray_destroy(dynstring); }

usize dynstring_size(const DynString* dynstring) { return dynarray_size(dynstring); }

String dynstring_view(const DynString* dynstring) {
  return dynarray_at(dynstring, 0, dynstring->size);
}

void dynstring_append(DynString* dynstring, String value) {
  mem_cpy(dynarray_push(dynstring, value.size), value);
}

void dynstring_append_char(DynString* dynstring, u8 val) { *dynarray_push_t(dynstring, u8) = val; }
