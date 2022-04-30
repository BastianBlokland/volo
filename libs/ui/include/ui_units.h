#pragma once

/**
 * Ui coordinate base.
 * For example 0.5 Canvas units means the middle of the canvas.
 */
typedef enum {
  UiBase_Absolute,
  UiBase_Current,
  UiBase_Container,
  UiBase_Canvas,
  UiBase_Input,
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
 * Layout flow direction.
 */
typedef enum {
  Ui_Right,
  Ui_Left,
  Ui_Up,
  Ui_Down,
} UiDir;

/**
 * Mask for filtering various layout operations.
 */
typedef enum {
  Ui_X  = 1 << 0,
  Ui_Y  = 1 << 1,
  Ui_XY = Ui_X | Ui_Y,
} UiAxis;

/**
 * Ui Glyph Layer.
 */
typedef enum {
  UiLayer_Normal,
  UiLayer_Invisible,
  UiLayer_Overlay,
} UiLayer;

/**
 * Ui Font Weight.
 * NOTE: These values are depended upon by the renderer.
 */
typedef enum {
  UiWeight_Light  = 0,
  UiWeight_Normal = 1,
  UiWeight_Bold   = 2,
  UiWeight_Heavy  = 3,
} UiWeight;

/**
 * Ui Clipping Mode.
 */
typedef enum {
  UiClip_None,
  UiClip_Rect,
} UiClip;
