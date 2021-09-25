#pragma once
#include "core_string.h"
#include "json_parse.h"

typedef enum {
  JsonTokenType_BracketOpen,
  JsonTokenType_BracketClose,
  JsonTokenType_CurlyOpen,
  JsonTokenType_CurlyClose,
  JsonTokenType_Comma,
  JsonTokenType_Colon,
  JsonTokenType_String,
  JsonTokenType_Number,
  JsonTokenType_True,
  JsonTokenType_False,
  JsonTokenType_Null,
  JsonTokenType_Error,
  JsonTokenType_End,
} JsonTokenType;

typedef struct {
  JsonTokenType type;
  union {
    String    val_string;
    f64       val_number;
    JsonError val_error;
  };
} JsonToken;

/**
 * Read a single json token.
 * Returns the remaining input.
 * The token is written to the output pointer.
 *
 * NOTE: String tokens are allocated in scratch memory, the caller is responsible for copying them
 * if they wish to persist them.
 */
String json_lex(String, JsonToken*);
