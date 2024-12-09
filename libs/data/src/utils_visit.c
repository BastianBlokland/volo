#include "core_bits.h"
#include "core_diag.h"
#include "data_utils.h"

#include "registry_internal.h"

typedef struct {
  const DataReg* reg;
  DataMeta       meta;
  Mem            data;
  DataType       visitType;
  void*          visitCtx;
  DataVisitor    visitor;
} VisitCtx;

static void data_visit_internal(const VisitCtx*);

static void data_visit_struct(const VisitCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const VisitCtx fieldCtx = {
        .reg       = ctx->reg,
        .meta      = fieldDecl->meta,
        .data      = data_field_mem(ctx->reg, fieldDecl, ctx->data),
        .visitType = ctx->visitType,
        .visitCtx  = ctx->visitCtx,
        .visitor   = ctx->visitor,
    };
    data_visit_internal(&fieldCtx);
  }
}

static void data_visit_union(const VisitCtx* ctx) {
  const DataDecl*       decl   = data_decl(ctx->reg, ctx->meta.type);
  const i32             tag    = *data_union_tag(&decl->val_union, ctx->data);
  const DataDeclChoice* choice = data_choice_from_tag(&decl->val_union, tag);
  diag_assert(choice);

  const bool emptyChoice = choice->meta.type == 0;
  if (!emptyChoice) {
    const VisitCtx choiceCtx = {
        .reg       = ctx->reg,
        .meta      = choice->meta,
        .data      = data_choice_mem(ctx->reg, choice, ctx->data),
        .visitType = ctx->visitType,
        .visitCtx  = ctx->visitCtx,
        .visitor   = ctx->visitor,
    };
    data_visit_internal(&choiceCtx);
  }
}

static void data_visit_single(const VisitCtx* ctx) {
  switch (data_decl(ctx->reg, ctx->meta.type)->kind) {
  case DataKind_Struct:
    data_visit_struct(ctx);
    break;
  case DataKind_Union:
    data_visit_union(ctx);
    break;
  default:
    break;
  }
  if (ctx->meta.type == ctx->visitType) {
    ctx->visitor(ctx->visitCtx, ctx->data);
  }
}

static void data_visit_pointer(const VisitCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  void*           ptr  = *mem_as_t(ctx->data, void*);
  if (ptr) {
    const VisitCtx subCtx = {
        .reg       = ctx->reg,
        .meta      = data_meta_base(ctx->meta),
        .data      = mem_create(ptr, decl->size),
        .visitType = ctx->visitType,
        .visitCtx  = ctx->visitCtx,
        .visitor   = ctx->visitor,
    };
    data_visit_single(&subCtx);
  }
}

static void data_visit_inline_array(const VisitCtx* ctx) {
  if (UNLIKELY(!ctx->meta.fixedCount)) {
    diag_crash_msg("Inline-arrays need at least 1 entry");
  }
  const usize expectedSize = data_meta_size(ctx->reg, ctx->meta);
  if (UNLIKELY(ctx->data.size != expectedSize)) {
    diag_crash_msg("Unexpected data-size for inline array");
  }
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  for (u16 i = 0; i != ctx->meta.fixedCount; ++i) {
    const VisitCtx elemCtx = {
        .reg       = ctx->reg,
        .meta      = data_meta_base(ctx->meta),
        .data      = mem_create(bits_ptr_offset(ctx->data.ptr, decl->size * i), decl->size),
        .visitType = ctx->visitType,
        .visitCtx  = ctx->visitCtx,
        .visitor   = ctx->visitor,
    };
    data_visit_single(&elemCtx);
  }
}

static void data_visit_heap_array(const VisitCtx* ctx) {
  const DataDecl*  decl  = data_decl(ctx->reg, ctx->meta.type);
  const HeapArray* array = mem_as_t(ctx->data, HeapArray);

  for (usize i = 0; i != array->count; ++i) {
    const VisitCtx elemCtx = {
        .reg       = ctx->reg,
        .meta      = data_meta_base(ctx->meta),
        .data      = data_elem_mem(decl, array, i),
        .visitType = ctx->visitType,
        .visitCtx  = ctx->visitCtx,
        .visitor   = ctx->visitor,
    };
    data_visit_single(&elemCtx);
  }
}

static void data_visit_dynarray(const VisitCtx* ctx) {
  const DynArray* array = mem_as_t(ctx->data, DynArray);

  for (usize i = 0; i != array->size; ++i) {
    const VisitCtx elemCtx = {
        .reg       = ctx->reg,
        .meta      = data_meta_base(ctx->meta),
        .data      = dynarray_at(array, i, 1),
        .visitType = ctx->visitType,
        .visitCtx  = ctx->visitCtx,
        .visitor   = ctx->visitor,
    };
    data_visit_single(&elemCtx);
  }
}

static void data_visit_internal(const VisitCtx* ctx) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    data_visit_single(ctx);
    return;
  case DataContainer_Pointer:
    data_visit_pointer(ctx);
    return;
  case DataContainer_InlineArray:
    data_visit_inline_array(ctx);
    return;
  case DataContainer_HeapArray:
    data_visit_heap_array(ctx);
    return;
  case DataContainer_DynArray:
    data_visit_dynarray(ctx);
    return;
  }
  diag_crash();
}

void data_visit(
    const DataReg* reg,
    const DataMeta meta,
    const Mem      data,
    const DataType visitType,
    void*          visitCtx,
    DataVisitor    visitor) {
  diag_assert(data.size == data_meta_size(reg, meta));

  const VisitCtx ctx = {
      .reg       = reg,
      .meta      = meta,
      .data      = data,
      .visitType = visitType,
      .visitCtx  = visitCtx,
      .visitor   = visitor,
  };
  data_visit_internal(&ctx);
}
