#pragma once
#include "core_annotation.h"
#include "core_types.h"

/**
 * Forward header for the core library.
 */

typedef enum eUnicode        Unicode;
typedef i16                  TimeZone;
typedef i32                  ThreadId;
typedef i32                  ThreadSpinLock;
typedef i64                  ProcessId;
typedef i64                  TimeDuration;
typedef i64                  TimeReal;
typedef i64                  TimeSteady;
typedef struct sAllocator    Allocator;
typedef struct sComplex      Complex;
typedef struct sDynArray     DynArray;
typedef struct sDynArray     DynString;
typedef struct sDynLib       DynLib;
typedef struct sFile         File;
typedef struct sFileIterator FileIterator;
typedef struct sFileMonitor  FileMonitor;
typedef struct sFormatArg    FormatArg;
typedef struct sHeapArray    HeapArray;
typedef struct sMem          BitSet;
typedef struct sMem          Mem;
typedef struct sMem          String;
typedef struct sProcess      Process;
typedef struct sRng          Rng;
typedef struct sSourceLoc    SourceLoc;
typedef struct sStringTable  StringTable;
typedef struct sSymbolStack  SymbolStack;
typedef u32                  StringHash;
typedef u32                  SymbolAddrRel;
typedef uptr                 SymbolAddr;
typedef uptr                 ThreadCondition;
typedef uptr                 ThreadHandle;
typedef uptr                 ThreadMutex;
typedef void*                Symbol;
