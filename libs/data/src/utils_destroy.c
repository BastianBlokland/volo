#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "data_utils.h"

#include "registry_internal.h"

typedef struct {
  const DataReg* reg;
  Allocator*     alloc;
  const DataMeta meta;
  const Mem      data;
} DestroyCtx;

static bool data_destroy_needed(const DataReg*, DataMeta);

static bool data_destroy_needed_single(const DataReg* reg, const DataMeta meta) {
  const DataDecl* decl = data_decl(reg, meta.type);
  switch (decl->kind) {
  case DataKind_bool:
  case DataKind_i8:
  case DataKind_i16:
  case DataKind_i32:
  case DataKind_i64:
  case DataKind_u8:
  case DataKind_u16:
  case DataKind_u32:
  case DataKind_u64:
  case DataKind_f16:
  case DataKind_f32:
  case DataKind_f64:
  case DataKind_Enum:
  case DataKind_StringHash:
  case DataKind_Opaque:
    return false;
  case DataKind_String:
    return (meta.flags & DataFlags_Intern) == 0;
  case DataKind_DataMem:
    return true;
  case DataKind_Struct: {
    dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
      if (data_destroy_needed(reg, fieldDecl->meta)) {
        return true;
      }
    }
    return false;
  }
  case DataKind_Union: {
    if (decl->val_union.nameOffset) {
      return true;
    }
    dynarray_for_t(&decl->val_union.choices, DataDeclChoice, choice) {
      const bool emptyChoice = choice->meta.type == 0;
      if (emptyChoice) {
        continue;
      }
      if (data_destroy_needed(reg, choice->meta)) {
        return true;
      }
    }
    return false;
  }
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static bool data_destroy_needed(const DataReg* reg, const DataMeta meta) {
  switch (meta.container) {
  case DataContainer_None:
  case DataContainer_InlineArray:
    return data_destroy_needed_single(reg, meta);
  case DataContainer_Pointer:
  case DataContainer_HeapArray:
  case DataContainer_DynArray:
    return true;
  }
  diag_crash();
}

static void data_destroy_internal(const DestroyCtx*);

static void data_destroy_string(const DestroyCtx* ctx) {
  if (!(ctx->meta.flags & DataFlags_Intern)) {
    const String val = *mem_as_t(ctx->data, String);
    string_maybe_free(ctx->alloc, val);
  }
}

static void data_destroy_mem(const DestroyCtx* ctx) {
  const DataMem val = *mem_as_t(ctx->data, DataMem);
  if (!val.external && mem_valid(data_mem(val))) {
    alloc_free(ctx->alloc, data_mem(val));
  }
}

static void data_destroy_struct(const DestroyCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const Mem fieldMem = data_field_mem(ctx->reg, fieldDecl, ctx->data);

    const DestroyCtx fieldCtx = {
        .reg   = ctx->reg,
        .alloc = ctx->alloc,
        .meta  = fieldDecl->meta,
        .data  = fieldMem,
    };
    data_destroy_internal(&fieldCtx);
  }
}

static void data_destroy_union(const DestroyCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  const i32       tag  = *data_union_tag(&decl->val_union, ctx->data);

  const String* name = data_union_name(&decl->val_union, ctx->data);
  if (name) {
    string_maybe_free(ctx->alloc, *name);
  }

  const DataDeclChoice* choice = data_choice_from_tag(&decl->val_union, tag);
  diag_assert(choice);

  const bool emptyChoice = choice->meta.type == 0;
  if (!emptyChoice) {
    const Mem choiceMem = data_choice_mem(ctx->reg, choice, ctx->data);

    const DestroyCtx fieldCtx = {
        .reg   = ctx->reg,
        .alloc = ctx->alloc,
        .meta  = choice->meta,
        .data  = choiceMem,
    };
    data_destroy_internal(&fieldCtx);
  }
}

static void data_destroy_single(const DestroyCtx* ctx) {
  switch (data_decl(ctx->reg, ctx->meta.type)->kind) {
  case DataKind_bool:
  case DataKind_i8:
  case DataKind_i16:
  case DataKind_i32:
  case DataKind_i64:
  case DataKind_u8:
  case DataKind_u16:
  case DataKind_u32:
  case DataKind_u64:
  case DataKind_f16:
  case DataKind_f32:
  case DataKind_f64:
  case DataKind_Enum:
  case DataKind_StringHash:
  case DataKind_Opaque:
    return;
  case DataKind_String:
    data_destroy_string(ctx);
    return;
  case DataKind_DataMem:
    data_destroy_mem(ctx);
    return;
  case DataKind_Struct:
    data_destroy_struct(ctx);
    return;
  case DataKind_Union:
    data_destroy_union(ctx);
    return;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static void data_destroy_pointer(const DestroyCtx* ctx) {
  void* ptr = *mem_as_t(ctx->data, void*);
  if (!ptr) {
    return;
  }
  const Mem targetMem = mem_create(ptr, data_size(ctx->reg, ctx->meta.type));

  const DestroyCtx subCtx = {
      .reg   = ctx->reg,
      .alloc = ctx->alloc,
      .meta  = data_meta_base(ctx->meta),
      .data  = targetMem,
  };
  data_destroy_single(&subCtx);

  alloc_free(ctx->alloc, targetMem);
}

static void data_destroy_inline_array(const DestroyCtx* ctx) {
  if (UNLIKELY(!ctx->meta.fixedCount)) {
    diag_crash_msg("Inline-arrays need at least 1 entry");
  }
  if (UNLIKELY(ctx->data.size != data_meta_size(ctx->reg, ctx->meta))) {
    diag_crash_msg("Unexpected data-size for inline array");
  }
  const DataMeta baseMeta = data_meta_base(ctx->meta);
  if (!data_destroy_needed(ctx->reg, baseMeta)) {
    return;
  }
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  for (u16 i = 0; i != ctx->meta.fixedCount; ++i) {
    const DestroyCtx elemCtx = {
        .reg   = ctx->reg,
        .alloc = ctx->alloc,
        .meta  = baseMeta,
        .data  = mem_create(bits_ptr_offset(ctx->data.ptr, decl->size * i), decl->size),
    };
    data_destroy_single(&elemCtx);
  }
}

static void data_destroy_heap_array(const DestroyCtx* ctx) {
  const DataDecl*  decl  = data_decl(ctx->reg, ctx->meta.type);
  const HeapArray* array = mem_as_t(ctx->data, HeapArray);
  if (!array->count) {
    return;
  }
  const DataMeta baseMeta = data_meta_base(ctx->meta);
  if (data_destroy_needed(ctx->reg, baseMeta)) {
    for (usize i = 0; i != array->count; ++i) {
      const DestroyCtx elemCtx = {
          .reg   = ctx->reg,
          .alloc = ctx->alloc,
          .meta  = baseMeta,
          .data  = data_elem_mem(decl, array, i),
      };
      data_destroy_single(&elemCtx);
    }
  }
  alloc_free(ctx->alloc, mem_create(array->values, decl->size * array->count));
}

static void data_destroy_dynarray(const DestroyCtx* ctx) {
  DynArray* array = mem_as_t(ctx->data, DynArray);

  const DataMeta baseMeta = data_meta_base(ctx->meta);
  if (data_destroy_needed(ctx->reg, baseMeta)) {
    for (usize i = 0; i != array->size; ++i) {
      const DestroyCtx elemCtx = {
          .reg   = ctx->reg,
          .alloc = ctx->alloc,
          .meta  = baseMeta,
          .data  = dynarray_at(array, i, 1),
      };
      data_destroy_single(&elemCtx);
    }
  }

  dynarray_destroy(array);
}

static void data_destroy_internal(const DestroyCtx* ctx) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    data_destroy_single(ctx);
    return;
  case DataContainer_Pointer:
    data_destroy_pointer(ctx);
    return;
  case DataContainer_InlineArray:
    data_destroy_inline_array(ctx);
    return;
  case DataContainer_HeapArray:
    data_destroy_heap_array(ctx);
    return;
  case DataContainer_DynArray:
    data_destroy_dynarray(ctx);
    return;
  }
  diag_crash();
}

void data_destroy(const DataReg* reg, Allocator* alloc, const DataMeta meta, const Mem data) {
  const DestroyCtx ctx = {
      .reg   = reg,
      .alloc = alloc,
      .meta  = meta,
      .data  = data,
  };
  data_destroy_internal(&ctx);
}
