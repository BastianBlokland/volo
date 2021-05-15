# --------------------------------------------------------------------------------------------------
# Libaries cmake file.
# --------------------------------------------------------------------------------------------------

add_library(volo_core STATIC
  core/src/placeholder.c)
target_include_directories(volo_core PUBLIC core/include)
