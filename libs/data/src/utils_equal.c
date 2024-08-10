#include "core_diag.h"
#include "data_utils.h"

#include "registry_internal.h"

typedef struct {
  const DataReg* reg;
  const DataMeta meta;
  const Mem      a, b;
} EqualCtx;

static bool data_equal_internal(const EqualCtx*);

static bool data_equal_string(const EqualCtx* ctx) {
  const String valA = *mem_as_t(ctx->a, String);
  const String valB = *mem_as_t(ctx->b, String);
  return string_eq(valA, valB);
}

static bool data_equal_mem(const EqualCtx* ctx) {
  const DataMem valA = *mem_as_t(ctx->a, DataMem);
  const DataMem valB = *mem_as_t(ctx->b, DataMem);
  return mem_eq(data_mem(valA), data_mem(valB));
}

static bool data_equal_struct(const EqualCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const Mem fieldMemA = data_field_mem(ctx->reg, fieldDecl, ctx->a);
    const Mem fieldMemB = data_field_mem(ctx->reg, fieldDecl, ctx->b);

    const EqualCtx fieldCtx = {
        .reg  = ctx->reg,
        .meta = fieldDecl->meta,
        .a    = fieldMemA,
        .b    = fieldMemB,
    };
    if (!data_equal_internal(&fieldCtx)) {
      return false;
    }
  }
  return true;
}

static bool data_equal_union(const EqualCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  const i32       tagA = *data_union_tag(&decl->val_union, ctx->a);
  const i32       tagB = *data_union_tag(&decl->val_union, ctx->b);
  if (tagA != tagB) {
    return false;
  }

  const String* nameA = data_union_name(&decl->val_union, ctx->a);
  const String* nameB = data_union_name(&decl->val_union, ctx->b);
  if (nameA && !string_eq(*nameA, *nameB)) {
    return false;
  }

  const DataDeclChoice* choice = data_choice_from_tag(&decl->val_union, tagA);
  diag_assert(choice);

  const bool emptyChoice = choice->meta.type == 0;
  if (!emptyChoice) {
    const Mem choiceMemA = data_choice_mem(ctx->reg, choice, ctx->a);
    const Mem choiceMemB = data_choice_mem(ctx->reg, choice, ctx->b);

    const EqualCtx choiceCtx = {
        .reg  = ctx->reg,
        .meta = choice->meta,
        .a    = choiceMemA,
        .b    = choiceMemB,
    };
    if (!data_equal_internal(&choiceCtx)) {
      return false;
    }
  }
  return true;
}

static bool data_equal_single(const EqualCtx* ctx) {
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
    return mem_eq(ctx->a, ctx->b);
  case DataKind_String:
    return data_equal_string(ctx);
  case DataKind_DataMem:
    return data_equal_mem(ctx);
  case DataKind_Struct:
    return data_equal_struct(ctx);
  case DataKind_Union:
    return data_equal_union(ctx);
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static bool data_equal_pointer(const EqualCtx* ctx) {
  void* ptrA = *mem_as_t(ctx->a, void*);
  void* ptrB = *mem_as_t(ctx->b, void*);
  if (!ptrA) {
    return ptrB == null;
  }
  if (!ptrB) {
    return false;
  }

  const DataDecl* decl    = data_decl(ctx->reg, ctx->meta.type);
  const Mem       ptrValA = mem_create(ptrA, decl->size);
  const Mem       ptrValB = mem_create(ptrB, decl->size);

  const EqualCtx subCtx = {
      .reg  = ctx->reg,
      .meta = data_meta_base(ctx->meta),
      .a    = ptrValA,
      .b    = ptrValB,
  };
  return data_equal_single(&subCtx);
}

static bool data_equal_array(const EqualCtx* ctx) {
  const DataDecl*  decl   = data_decl(ctx->reg, ctx->meta.type);
  const DataArray* arrayA = mem_as_t(ctx->a, DataArray);
  const DataArray* arrayB = mem_as_t(ctx->b, DataArray);
  if (arrayA->count != arrayB->count) {
    return false;
  }

  for (usize i = 0; i != arrayA->count; ++i) {
    const EqualCtx elemCtx = {
        .reg  = ctx->reg,
        .meta = data_meta_base(ctx->meta),
        .a    = data_elem_mem(decl, arrayA, i),
        .b    = data_elem_mem(decl, arrayB, i),
    };
    if (!data_equal_single(&elemCtx)) {
      return false;
    }
  }
  return true;
}

static bool data_equal_dynarray(const EqualCtx* ctx) {
  const DynArray* arrayA = mem_as_t(ctx->a, DynArray);
  const DynArray* arrayB = mem_as_t(ctx->b, DynArray);
  if (arrayA->size != arrayB->size) {
    return false;
  }

  for (usize i = 0; i != arrayA->size; ++i) {
    const EqualCtx elemCtx = {
        .reg  = ctx->reg,
        .meta = data_meta_base(ctx->meta),
        .a    = dynarray_at(arrayA, i, 1),
        .b    = dynarray_at(arrayB, i, 1),
    };
    if (!data_equal_single(&elemCtx)) {
      return false;
    }
  }
  return true;
}

static bool data_equal_internal(const EqualCtx* ctx) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    return data_equal_single(ctx);
  case DataContainer_Pointer:
    return data_equal_pointer(ctx);
  case DataContainer_DataArray:
    return data_equal_array(ctx);
  case DataContainer_DynArray:
    return data_equal_dynarray(ctx);
  }
  diag_crash();
}

bool data_equal(const DataReg* reg, const DataMeta meta, const Mem a, const Mem b) {
  const EqualCtx ctx = {
      .reg  = reg,
      .meta = meta,
      .a    = a,
      .b    = b,
  };
  return data_equal_internal(&ctx);
}
