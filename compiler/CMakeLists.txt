add_executable(perry-clang perry-clang.cpp)

add_custom_command(TARGET perry-clang
  POST_BUILD COMMAND ln -sf "perry-clang" "perry-clang++")

target_link_libraries(perry-clang LLVMSupport)