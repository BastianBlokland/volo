#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "data_utils.h"

#include "registry_internal.h"

typedef struct {
  const DataReg* reg;
  Allocator*     alloc;
  const DataMeta meta;
  const Mem      original, clone;
} CloneCtx;

static void data_clone_internal(const CloneCtx*);

static void data_clone_string(const CloneCtx* ctx) {
  const String originalVal = *mem_as_t(ctx->original, String);
  if (ctx->meta.flags & DataFlags_Intern) {
    *mem_as_t(ctx->clone, String) = originalVal;
  } else {
    *mem_as_t(ctx->clone, String) = string_maybe_dup(ctx->alloc, originalVal);
  }
}

static usize data_clone_mem_align(const usize size) {
  const usize biggestPow2 = u64_lit(1) << bits_ctz(size);
  return math_min(biggestPow2, data_type_mem_align_max);
}

static void data_clone_mem(const CloneCtx* ctx) {
  const Mem originalMem = *mem_as_t(ctx->original, Mem);
  if (mem_valid(originalMem)) {
    const usize align          = data_clone_mem_align(originalMem.size);
    *mem_as_t(ctx->clone, Mem) = alloc_dup(ctx->alloc, originalMem, align);
  } else {
    *mem_as_t(ctx->clone, Mem) = mem_empty;
  }
}

static void data_clone_struct(const CloneCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  mem_set(ctx->clone, 0); // Initialize non-specified memory to zero.

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const Mem originalFieldMem = data_field_mem(ctx->reg, fieldDecl, ctx->original);
    const Mem dataFieldMem     = data_field_mem(ctx->reg, fieldDecl, ctx->clone);

    const CloneCtx fieldCtx = {
        .reg      = ctx->reg,
        .alloc    = ctx->alloc,
        .meta     = fieldDecl->meta,
        .original = originalFieldMem,
        .clone    = dataFieldMem,
    };
    data_clone_internal(&fieldCtx);
  }
}

static void data_clone_union(const CloneCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  const i32       tag  = *data_union_tag(&decl->val_union, ctx->original);

  mem_set(ctx->clone, 0); // Initialize non-specified memory to zero.

  *data_union_tag(&decl->val_union, ctx->clone) = tag;

  const String* name = data_union_name(&decl->val_union, ctx->original);
  if (name) {
    *data_union_name(&decl->val_union, ctx->clone) = string_maybe_dup(ctx->alloc, *name);
  }

  const DataDeclChoice* choice = data_choice_from_tag(&decl->val_union, tag);
  diag_assert(choice);

  const bool emptyChoice = choice->meta.type == 0;
  if (!emptyChoice) {
    const Mem originalChoiceMem = data_choice_mem(ctx->reg, choice, ctx->original);
    const Mem choiceMem         = data_choice_mem(ctx->reg, choice, ctx->clone);

    const CloneCtx choiceCtx = {
        .reg      = ctx->reg,
        .alloc    = ctx->alloc,
        .meta     = choice->meta,
        .original = originalChoiceMem,
        .clone    = choiceMem,
    };
    data_clone_internal(&choiceCtx);
  }
}

static void data_clone_single(const CloneCtx* ctx) {
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
  case DataKind_f32:
  case DataKind_f64:
  case DataKind_Enum:
    mem_cpy(ctx->clone, ctx->original);
    return;
  case DataKind_String:
    data_clone_string(ctx);
    return;
  case DataKind_Mem:
    data_clone_mem(ctx);
    return;
  case DataKind_Struct:
    data_clone_struct(ctx);
    return;
  case DataKind_Union:
    data_clone_union(ctx);
    return;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static void data_clone_pointer(const CloneCtx* ctx) {
  void* originalPtr = *mem_as_t(ctx->original, void*);
  if (!originalPtr) {
    *mem_as_t(ctx->clone, void*) = null;
    return;
  }

  const DataDecl* decl         = data_decl(ctx->reg, ctx->meta.type);
  const Mem       originalMem  = mem_create(originalPtr, decl->size);
  const Mem       newMem       = alloc_alloc(ctx->alloc, decl->size, decl->align);
  *mem_as_t(ctx->clone, void*) = newMem.ptr;

  const CloneCtx subCtx = {
      .reg      = ctx->reg,
      .alloc    = ctx->alloc,
      .meta     = data_meta_base(ctx->meta),
      .original = originalMem,
      .clone    = newMem,
  };
  data_clone_single(&subCtx);
}

static void data_clone_array(const CloneCtx* ctx) {
  const DataDecl*  decl          = data_decl(ctx->reg, ctx->meta.type);
  const DataArray* originalArray = mem_as_t(ctx->original, DataArray);
  const usize      count         = originalArray->count;
  if (!count) {
    *mem_as_t(ctx->clone, DataArray) = (DataArray){0};
    return;
  }

  const Mem  newArrayMem = alloc_alloc(ctx->alloc, decl->size * count, decl->align);
  DataArray* newArray    = mem_as_t(ctx->clone, DataArray);
  *newArray              = (DataArray){.values = newArrayMem.ptr, .count = count};

  for (usize i = 0; i != count; ++i) {
    const Mem originalElemMem = data_elem_mem(decl, originalArray, i);
    const Mem newElemMem      = data_elem_mem(decl, newArray, i);

    const CloneCtx elemCtx = {
        .reg      = ctx->reg,
        .alloc    = ctx->alloc,
        .meta     = data_meta_base(ctx->meta),
        .original = originalElemMem,
        .clone    = newElemMem,
    };
    data_clone_single(&elemCtx);
  }
}

static void data_clone_internal(const CloneCtx* ctx) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    data_clone_single(ctx);
    return;
  case DataContainer_Pointer:
    data_clone_pointer(ctx);
    return;
  case DataContainer_Array:
    data_clone_array(ctx);
    return;
  }
  diag_crash();
}

void data_clone(
    const DataReg* reg,
    Allocator*     alloc,
    const DataMeta meta,
    const Mem      original,
    const Mem      clone) {

  const CloneCtx ctx = {
      .reg      = reg,
      .alloc    = alloc,
      .meta     = meta,
      .original = original,
      .clone    = clone,
  };
  data_clone_internal(&ctx);
}
