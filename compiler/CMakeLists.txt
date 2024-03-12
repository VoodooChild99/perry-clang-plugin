add_executable(perry-clang perry-clang.cpp)

target_compile_definitions(perry-clang PRIVATE
  C_COMPILER_BIN="${CMAKE_C_COMPILER}" CXX_COMPILER_BIN="${CMAKE_CXX_COMPILER}")

add_custom_command(TARGET perry-clang
  POST_BUILD COMMAND ln -sf "perry-clang" "perry-clang++")

target_link_libraries(perry-clang LLVMSupport)