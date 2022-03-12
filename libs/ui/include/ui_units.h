#pragma once

/**
 * Ui coordinate base.
 * For example 0.5 Window units means the middle of the window.
 */
typedef enum {
  UiBase_Absolute,
  UiBase_Current,
  UiBase_Container,
  UiBase_Window,
  UiBase_Cursor,
} UiBase;

/**
 * Alignment relative to a rectangle.
 */
typedef enum {
  UiAlign_TopLeft,
  UiAlign_TopCenter,
  UiAlign_TopRight,
  UiAlign_MiddleLeft,
  UiAlign_MiddleCenter,
  UiAlign_MiddleRight,
  UiAlign_BottomLeft,
  UiAlign_BottomCenter,
  UiAlign_BottomRight,
} UiAlign;

/**
 * Mask for filtering various layout operations.
 */
typedef enum {
  Ui_X  = 1 << 0,
  Ui_Y  = 1 << 1,
  Ui_XY = Ui_X | Ui_Y,
} UiAxis;
