#include "core_annotation.h"
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

void dynstring_append(DynString* dynstring, const String value) {
  mem_cpy(dynarray_push(dynstring, value.size), value);
}

void dynstring_append_char(DynString* dynstring, const u8 val) {
  *dynarray_push_t(dynstring, u8) = val;
}

void dynstring_append_chars(DynString* dynstring, const u8 val, const usize amount) {
  mem_set(dynarray_push(dynstring, amount), val);
}

void dynstring_insert(DynString* dynstring, const String str, const usize idx) {
  mem_cpy(dynarray_insert(dynstring, idx, str.size), str);
}

void dynstring_insert_chars(
    DynString* dynstring, const u8 val, const usize idx, const usize amount) {
  mem_set(dynarray_insert(dynstring, idx, amount), val);
}

void dynstring_erase_chars(DynString* dynstring, const usize idx, const usize amount) {
  dynarray_remove(dynstring, idx, amount);
}

String dynstring_push(DynString* dynstring, const usize amount) {
  return dynarray_push(dynstring, amount);
}
