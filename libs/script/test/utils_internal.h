#pragma once
#include "check_spec.h"
#include "script_lex.h"

#define tok_simple(_TYPE_)                                                                         \
  (ScriptToken) { .type = ScriptTokenType_##_TYPE_ }

#define tok_null()                                                                                 \
  (ScriptToken) { .type = ScriptTokenType_LitNull }

#define tok_number(_VAL_)                                                                          \
  (ScriptToken) { .type = ScriptTokenType_LitNumber, .val_number = (_VAL_) }

#define tok_bool(_VAL_)                                                                            \
  (ScriptToken) { .type = ScriptTokenType_LitBool, .val_bool = (_VAL_) }

#define tok_key(_VAL_)                                                                             \
  (ScriptToken) { .type = ScriptTokenType_LitKey, .val_key = string_hash(_VAL_) }

#define tok_key_lit(_VAL_)                                                                         \
  (ScriptToken) { .type = ScriptTokenType_LitKey, .val_key = string_hash_lit(_VAL_) }

#define tok_err(_ERR_)                                                                             \
  (ScriptToken) { .type = ScriptTokenType_Error, .val_error = (_ERR_) }

#define tok_end()                                                                                  \
  (ScriptToken) { .type = ScriptTokenType_End }

#define check_eq_tok(_A_, _B_) check_eq_tok_impl(_testCtx, (_A_), (_B_), source_location())
#define check_neq_tok(_A_, _B_) check_neq_tok_impl(_testCtx, (_A_), (_B_), source_location())

void check_eq_tok_impl(CheckTestContext*, const ScriptToken* a, const ScriptToken* b, SourceLoc);
void check_neq_tok_impl(CheckTestContext*, const ScriptToken* a, const ScriptToken* b, SourceLoc);
