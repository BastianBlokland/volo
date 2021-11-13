# --------------------------------------------------------------------------------------------------
# Data library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: data")

add_library(volo_data STATIC
  src/dummy.c)
target_include_directories(volo_data PUBLIC include)
target_link_libraries(volo_data PUBLIC volo_core)
target_link_libraries(volo_data PRIVATE volo_json)
