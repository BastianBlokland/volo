#pragma once
#include "net.h"

typedef enum eNetDir {
  NetDir_In   = 1 << 0,
  NetDir_Out  = 1 << 1,
  NetDir_Both = NetDir_In | NetDir_Out,
} NetDir;
