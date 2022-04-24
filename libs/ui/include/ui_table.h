#pragma once
#include "ecs_module.h"
#include "ui_rect.h"
#include "ui_units.h"

ecs_comp_extern(UiCanvasComp);

#define ui_table_max_columns 8

typedef enum {
  UiTableColumn_Fixed,
  UiTableColumn_Flexible, // NOTE: Can only be used on the last column.
} UiTableColumnType;

typedef struct {
  UiTableColumnType type;
  f32               width;
} UiTableColumn;

typedef struct {
  UiBase        parent;
  UiAlign       align;
  f32           rowHeight;
  UiVector      spacing;
  UiTableColumn columns[ui_table_max_columns];
  u32           columnCount;
  u32           column;
  u32           row;
} UiTable;

/**
 * Create a layout table.
 */
#define ui_table(...)                                                                              \
  ((UiTable){                                                                                      \
      .parent    = UiBase_Container,                                                               \
      .align     = UiAlign_TopLeft,                                                                \
      .rowHeight = 25,                                                                             \
      .spacing   = ui_vector(10, 10),                                                              \
      .column    = sentinel_u32,                                                                   \
      .row       = sentinel_u32,                                                                   \
      __VA_ARGS__})

void ui_table_add_column(UiTable*, UiTableColumnType, f32 width);
void ui_table_next_column(UiCanvasComp*, UiTable*);
void ui_table_next_row(UiCanvasComp*, UiTable*);
