#pragma once
#include "core.h"

/**
 * Forward header for the script library.
 */

typedef enum eScriptCompileError     ScriptCompileError;
typedef enum eScriptDiagFilter       ScriptDiagFilter;
typedef enum eScriptDiagKind         ScriptDiagKind;
typedef enum eScriptDiagSeverity     ScriptDiagSeverity;
typedef enum eScriptExprKind         ScriptExprKind;
typedef enum eScriptIntrinsic        ScriptIntrinsic;
typedef enum eScriptLexFlags         ScriptLexFlags;
typedef enum eScriptOp               ScriptOp;
typedef enum eScriptPanicKind        ScriptPanicKind;
typedef enum eScriptSigArgFlags      ScriptSigArgFlags;
typedef enum eScriptSymKind          ScriptSymKind;
typedef enum eScriptSymRefKind       ScriptSymRefKind;
typedef enum eScriptTokenKind        ScriptTokenKind;
typedef enum eScriptType             ScriptType;
typedef struct sScriptBinder         ScriptBinder;
typedef struct sScriptBinderCall     ScriptBinderCall;
typedef struct sScriptDiag           ScriptDiag;
typedef struct sScriptDiagBag        ScriptDiagBag;
typedef struct sScriptDoc            ScriptDoc;
typedef struct sScriptEnum           ScriptEnum;
typedef struct sScriptFormatSettings ScriptFormatSettings;
typedef struct sScriptLexKeyword     ScriptLexKeyword;
typedef struct sScriptLookup         ScriptLookup;
typedef struct sScriptMem            ScriptMem;
typedef struct sScriptPanic          ScriptPanic;
typedef struct sScriptPanicHandler   ScriptPanicHandler;
typedef struct sScriptProgram        ScriptProgram;
typedef struct sScriptRange          ScriptRange;
typedef struct sScriptRangeLineCol   ScriptRangeLineCol;
typedef struct sScriptSig            ScriptSig;
typedef struct sScriptSigArg         ScriptSigArg;
typedef struct sScriptSymBag         ScriptSymBag;
typedef struct sScriptSymRef         ScriptSymRef;
typedef struct sScriptToken          ScriptToken;
typedef struct sScriptVal            ScriptVal;
typedef u16                          ScriptBinderSlot;
typedef u16                          ScriptMask;
typedef u32                          ScriptExpr;
typedef u32                          ScriptPos;
typedef u32                          ScriptScopeId;
typedef u64                          ScriptBinderHash;
typedef u8                           ScriptVarId;
