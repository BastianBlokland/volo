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

static void data_destroy_array(const DestroyCtx* ctx) {
  const DataDecl*  decl  = data_decl(ctx->reg, ctx->meta.type);
  const DataArray* array = mem_as_t(ctx->data, DataArray);
  if (!array->count) {
    return;
  }

  for (usize i = 0; i != array->count; ++i) {
    const DestroyCtx elemCtx = {
        .reg   = ctx->reg,
        .alloc = ctx->alloc,
        .meta  = data_meta_base(ctx->meta),
        .data  = data_elem_mem(decl, array, i),
    };
    data_destroy_single(&elemCtx);
  }

  alloc_free(ctx->alloc, mem_create(array->values, decl->size * array->count));
}

static void data_destroy_dynarray(const DestroyCtx* ctx) {
  DynArray* array = mem_as_t(ctx->data, DynArray);

  for (usize i = 0; i != array->size; ++i) {
    const DestroyCtx elemCtx = {
        .reg   = ctx->reg,
        .alloc = ctx->alloc,
        .meta  = data_meta_base(ctx->meta),
        .data  = dynarray_at(array, i, 1),
    };
    data_destroy_single(&elemCtx);
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
  case DataContainer_DataArray:
    data_destroy_array(ctx);
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
