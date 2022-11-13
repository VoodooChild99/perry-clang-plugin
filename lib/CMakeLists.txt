add_library(perry-clang-plugin SHARED PerryClangPlugin.cpp)

target_include_directories(perry-clang-plugin
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)

target_link_libraries(perry-clang-plugin
  "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>"
)