#pragma once
#include "core_string.h"
#include "xml_read.h"

typedef enum {
  XmlTokenType_Error,
  XmlTokenType_End,
} XmlTokenType;

typedef struct {
  XmlTokenType type;
  union {
    XmlError val_error;
  };
} XmlToken;

/**
 * Read a single xml token.
 * Returns the remaining input.
 * The token is written to the output pointer.
 *
 * NOTE: String tokens are allocated in scratch memory, the caller is responsible for copying them
 * if they wish to persist them.
 */
String xml_lex(String, XmlToken*);
