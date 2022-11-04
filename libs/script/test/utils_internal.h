#pragma once
#include "check_spec.h"
#include "script_doc.h"
#include "script_lex.h"

// clang-format off

#define tok_simple(_TYPE_) (ScriptToken){.type = ScriptTokenType_##_TYPE_}
#define tok_null()         (ScriptToken){.type = ScriptTokenType_Null}
#define tok_number(_VAL_)  (ScriptToken){.type = ScriptTokenType_Number, .val_number = (_VAL_)}
#define tok_bool(_VAL_)    (ScriptToken){.type = ScriptTokenType_Bool, .val_bool = (_VAL_)}
#define tok_id(_VAL_)      (ScriptToken){.type = ScriptTokenType_Identifier, .val_identifier = string_hash(_VAL_)}
#define tok_id_lit(_VAL_)  (ScriptToken){.type = ScriptTokenType_Identifier, .val_identifier = string_hash_lit(_VAL_)}
#define tok_key(_VAL_)     (ScriptToken){.type = ScriptTokenType_Key, .val_key = string_hash(_VAL_)}
#define tok_key_lit(_VAL_) (ScriptToken){.type = ScriptTokenType_Key, .val_key = string_hash_lit(_VAL_)}
#define tok_err(_ERR_)     (ScriptToken){.type = ScriptTokenType_Error, .val_error = ScriptError_##_ERR_}
#define tok_end()          (ScriptToken){.type = ScriptTokenType_End}

#define check_eq_tok(_A_, _B_)                   check_eq_tok_impl(_testCtx, (_A_), (_B_), source_location())
#define check_neq_tok(_A_, _B_)                  check_neq_tok_impl(_testCtx, (_A_), (_B_), source_location())
#define check_truthy(_VAL_)                      check_truthy_impl(_testCtx, (_VAL_), source_location())
#define check_falsy(_VAL_)                       check_falsy_impl(_testCtx, (_VAL_), source_location())
#define check_eq_val(_A_, _B_)                   check_eq_val_impl(_testCtx, (_A_), (_B_), source_location())
#define check_neq_val(_A_, _B_)                  check_neq_val_impl(_testCtx, (_A_), (_B_), source_location())
#define check_less_val(_A_, _B_)                 check_less_val_impl(_testCtx, (_A_), (_B_), source_location())
#define check_greater_val(_A_, _B_)              check_greater_val_impl(_testCtx, (_A_), (_B_), source_location())
#define check_expr_str(_DOC_, _EXPR_, _STR_)     check_expr_str_impl(_testCtx, (_DOC_), (_EXPR_), (_STR_), source_location())
#define check_expr_str_lit(_DOC_, _EXPR_, _STR_) check_expr_str_impl(_testCtx, (_DOC_), (_EXPR_), string_lit(_STR_), source_location())

// clang-format on

void check_eq_tok_impl(CheckTestContext*, const ScriptToken* a, const ScriptToken* b, SourceLoc);
void check_neq_tok_impl(CheckTestContext*, const ScriptToken* a, const ScriptToken* b, SourceLoc);
void check_truthy_impl(CheckTestContext*, ScriptVal, SourceLoc);
void check_falsy_impl(CheckTestContext*, ScriptVal, SourceLoc);
void check_eq_val_impl(CheckTestContext*, ScriptVal a, ScriptVal b, SourceLoc);
void check_neq_val_impl(CheckTestContext*, ScriptVal a, ScriptVal b, SourceLoc);
void check_less_val_impl(CheckTestContext*, ScriptVal a, ScriptVal b, SourceLoc);
void check_greater_val_impl(CheckTestContext*, ScriptVal a, ScriptVal b, SourceLoc);
void check_expr_str_impl(CheckTestContext*, const ScriptDoc*, ScriptExpr, String expect, SourceLoc);
