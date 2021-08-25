#pragma once
#include "core_string.h"

typedef enum {
  JsonTokenType_CurlyOpen,
  JsonTokenType_CurlyClose,
  JsonTokenType_BracketOpen,
  JsonTokenType_BracketClose,
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

typedef enum {
  JsonTokenError_InvalidChar,
  JsonTokenError_InvalidCharInNull,
  JsonTokenError_InvalidCharInTrue,
  JsonTokenError_InvalidCharInFalse,
  JsonTokenError_TooLongString,
  JsonTokenError_MissingQuoteInString,
  JsonTokenError_InvalidCharInString,
} JsonTokenError;

typedef struct {
  JsonTokenType type;
  union {
    String         val_string;
    f64            val_number;
    JsonTokenError val_error;
  };
} JsonToken;

/**
 * Read a single json token.
 * Returns the remaining input.
 * The token is written to the output pointer.
 *
 * Note: String tokens are allocated in scratch memory, the caller is responsible for copying them
 * if they whish to persist them.
 */
String json_lex(String, JsonToken*);
