#include "core_dynstring.h"

DynString dynstring_create(Allocator* alloc, usize capacity) {
  return dynarray_create(alloc, 1, 1, capacity);
}

DynString dynstring_create_over(Mem memory) { return dynarray_create_over(memory, 1u); }

void dynstring_destroy(DynString* dynstring) { dynarray_destroy(dynstring); }

usize dynstring_size(const DynString* dynstring) { return dynarray_size(dynstring); }

String dynstring_view(const DynString* dynstring) {
  return dynarray_at(dynstring, 0, dynstring->size);
}

void dynstring_clear(DynString* dynstring) { dynarray_clear(dynstring); }

void dynstring_append(DynString* dynstring, String value) {
  mem_cpy(dynarray_push(dynstring, value.size), value);
}

void dynstring_append_char(DynString* dynstring, u8 val) { *dynarray_push_t(dynstring, u8) = val; }

void dynstring_append_chars(DynString* dynstring, u8 val, usize amount) {
  mem_set(dynarray_push(dynstring, amount), val);
}

void dynstring_insert_chars(DynString* dynstring, u8 val, usize idx, usize amount) {
  mem_set(dynarray_insert(dynstring, idx, amount), val);
}
