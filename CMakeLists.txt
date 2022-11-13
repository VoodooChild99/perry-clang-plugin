# borrowed from `clang-tutor`(https://github.com/banach-space/clang-tutor)

cmake_minimum_required(VERSION 3.24.1)
project(perry-clang-plugin)

#===============================================================================
# GET CLANG INSTALLATION DIR
#===============================================================================
# In clang-tutor, `CT_Clang_INSTALL_DIR` is the key CMake variable - it points
# to a Clang installation directory. For the sake of completeness,
# <PackageName>_DIR (i.e. `Clang_DIR`) and <PackageName>_ROOT (i.e.
# `Clang_ROOT`) are also supported. Visit CMake documentation for more details:
#   https://cmake.org/cmake/help/latest/command/find_package.html
# Use only _one_ of these variables.

if(DEFINED Clang_ROOT)
  set(PERRY_CLANG_PACKAGE_DIR "${Clang_ROOT}/../../..")
elseif(DEFINED Clang_DIR)
  set(PERRY_CLANG_PACKAGE_DIR "${Clang_DIR}/../../..")
endif()
mark_as_advanced(PERRY_CLANG_PACKAGE_DIR)

# Set this to a valid Clang installation directory. This is most likely where
# LLVM is installed on your system.
set(PERRY_Clang_INSTALL_DIR "${PERRY_CLANG_PACKAGE_DIR}" CACHE PATH
  "Clang installation directory")

#===============================================================================
# VERIFY CLANG INSTALLATION DIR
#===============================================================================
set(PERRY_LLVM_INCLUDE_DIR "${PERRY_Clang_INSTALL_DIR}/include/llvm")
if(NOT EXISTS "${PERRY_LLVM_INCLUDE_DIR}")
message(FATAL_ERROR
  " PERRY_Clang_INSTALL_DIR (${PERRY_LLVM_INCLUDE_DIR}) is invalid.")
endif()

set(PERRY_LLVM_CMAKE_FILE
  "${PERRY_Clang_INSTALL_DIR}/lib/cmake/clang/ClangConfig.cmake")
if(NOT EXISTS "${PERRY_LLVM_CMAKE_FILE}")
message(FATAL_ERROR
  " PERRY_LLVM_CMAKE_FILE (${PERRY_LLVM_CMAKE_FILE}) is invalid.")
endif()

#===============================================================================
# LOAD LLVM CONFIGURATION
#===============================================================================
# Add the location of ClangConfig.cmake to CMake search paths (so that
# find_package can locate it)
list(APPEND CMAKE_PREFIX_PATH "${PERRY_Clang_INSTALL_DIR}/lib/cmake/clang/")

find_package(Clang REQUIRED CONFIG)

message(STATUS "Found Clang ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using ClangConfig.cmake in: ${PERRY_Clang_INSTALL_DIR}")

message("CLANG STATUS:
  Includes (clang)    ${CLANG_INCLUDE_DIRS}
  Includes (llvm)     ${LLVM_INCLUDE_DIRS}
  Libraries (llvm)    ${LLVM_LIBRARY_DIRS}
  Targets (llvm)      ${LLVM_TARGETS_TO_BUILD}
  Definitions (llvm)  ${LLVM_DEFINITIONS}"
)

# HelloWorld includes headers from LLVM and Clang - update the include paths
# accordingly
include_directories(SYSTEM "${LLVM_INCLUDE_DIRS};${CLANG_INCLUDE_DIRS}")
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

#===============================================================================
# LLVM-TUTOR BUILD CONFIGURATION
#===============================================================================
# Use the same C++ standard as LLVM does
set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE
      STRING "Build type (default Debug):" FORCE)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall\
    -fdiagnostics-color=always")

# LLVM is normally built without RTTI. Be consistent with that.
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# -fvisibility-inlines-hidden is set when building LLVM and on Darwin warnings
# are triggered if llvm-tutor is built without this flag (though otherwise it
# builds fine). For consistency, add it here too.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fvisibility-inlines-hidden"
  SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
if(${SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG} EQUAL "1")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")
endif()

# Generate Compilation Database
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#===============================================================================
# ADD THE TARGET
#===============================================================================
add_subdirectory(lib)
add_subdirectory(compiler)