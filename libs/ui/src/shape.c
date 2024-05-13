#include "core_alloc.h"
#include "core_utf8.h"
#include "ui_shape.h"

String ui_shape_scratch(const Unicode cp) {
  u8* scratch = alloc_alloc(g_allocScratch, 4, 1).ptr;
  return (String){.ptr = scratch, .size = utf8_cp_write(scratch, cp)};
}
