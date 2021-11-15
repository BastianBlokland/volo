#include "core_bits.h"
#include "core_diag.h"

#include "utils_internal.h"

static usize data_utils_size(const DataMeta meta) {
  switch (meta.container) {
  case DataContainer_None:
    return data_type_size(meta.type);
  case DataContainer_Array:
    return sizeof(DataArray);
  }
  diag_crash();
}

Mem data_utils_field_mem(const DataDeclField* field, Mem structMem) {
  return mem_create(bits_ptr_offset(structMem.ptr, field->offset), data_utils_size(field->meta));
}
