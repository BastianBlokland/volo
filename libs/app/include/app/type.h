#pragma once

/**
 * Application type.
 * - Console: Expected to use standard file handles (stdIn, stdOut, stdErr) to interact with.
 * - Gui (Graphical User Interface): Expected to open a window to interact with.
 */
typedef enum {
  AppType_Console,
  AppType_Gui,
} AppType;
