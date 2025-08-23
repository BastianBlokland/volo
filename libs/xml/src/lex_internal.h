#pragma once
#include "core/string.h"
#include "xml/read.h"

typedef enum {
  XmlTokenType_DeclStart,
  XmlTokenType_DeclClose,
  XmlTokenType_TagStart,
  XmlTokenType_TagEnd,
  XmlTokenType_TagClose,
  XmlTokenType_TagEndClose,
  XmlTokenType_Equal,
  XmlTokenType_String,
  XmlTokenType_Name,
  XmlTokenType_Comment,
  XmlTokenType_Content,
  XmlTokenType_Error,
  XmlTokenType_End,
} XmlTokenType;

typedef struct {
  XmlTokenType type;
  union {
    String   val_decl;
    String   val_tag;
    String   val_string;
    String   val_name;
    String   val_comment;
    String   val_content;
    XmlError val_error;
  };
} XmlToken;

typedef enum {
  XmlPhase_Content,
  XmlPhase_Markup,
} XmlPhase;

/**
 * Read a single xml token.
 * Returns the remaining input.
 * The token is written to the output pointer.
 *
 * NOTE: This lexer supports a subset of Xml 1.0 (https://www.w3.org/TR/2008/REC-xml-20081126/).
 * NOTE: String tokens are allocated in the original input or scratch memory, the caller is
 * responsible for copying them if they wish to persist them.
 */
String xml_lex(String, XmlPhase, XmlToken*);
