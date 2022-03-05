#include "core_alloc.h"
#include "core_utf8.h"
#include "ui_shape.h"

String ui_shape_scratch(const Unicode cp) {
  Mem       scratch   = alloc_alloc(g_alloc_scratch, 4, 1);
  DynString dynString = dynstring_create_over(scratch);
  utf8_cp_write(&dynString, cp);
  return dynstring_view(&dynString);
}
