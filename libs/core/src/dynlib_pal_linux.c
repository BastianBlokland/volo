#include "core_alloc.h"
#include "core_dynlib.h"

#include <dlfcn.h>
#include <limits.h>

void dynlib_init() {}

struct sDynLib {
  void*      handle;
  Allocator* alloc;
};

DynLibResult dynlib_load(Allocator* alloc, const String name, DynLib** out) {
  // Copy the name on the stack and null-terminate it.
  if (name.size >= PATH_MAX) {
    return DynLibResult_NameTooLong;
  }
  Mem nameBuffer = mem_stack(PATH_MAX);
  mem_cpy(nameBuffer, name);
  *mem_at_u8(nameBuffer, name.size) = '\0';

  void* handle = dlopen(name.ptr, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    return DynLibResult_LibraryNotFound;
  }

  *out  = alloc_alloc_t(alloc, DynLib);
  **out = (DynLib){
      .handle = handle,
      .alloc  = alloc,
  };
  return DynLibResult_Success;
}
