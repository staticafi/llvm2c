cmake_minimum_required(VERSION 3.1)

# FIXME: Unfortunately, C is (at least temporarily) required due to a bug
# in LLVM 14.  See https://github.com/llvm/llvm-project/issues/53950.
project(llvm2c LANGUAGES C CXX)

# --------------------------------------------------
# Build type
# --------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Build type not set. Setting default.")
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "RelWithDebInfo"
                                                     "MinSizeRel" "Release")

# --------------------------------------------------
# Compilation flags
# --------------------------------------------------
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall -Wextra -pedantic -fno-rtti")

# --------------------------------------------------
# LLVM
# --------------------------------------------------
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if(${LLVM_PACKAGE_VERSION} VERSION_LESS "5.0")
  message(FATAL_ERROR "LLVM 5.0 or newer is required!")
endif()

message(STATUS "LLVM include dir: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM libraries dir: ${LLVM_LIBRARY_DIRS}")
message(STATUS "LLVM definitions: ${LLVM_DEFINITIONS}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

option(LLVM_LINK_DYLIB "Link with LLVM dynamically" ON)
if(LLVM_LINK_DYLIB)
  message(STATUS "LLVM linking: dynamic")
  set(llvm_libs LLVM)
else()
  message(STATUS "LLVM linking: static")
  # Find the libraries that correspond to the LLVM components
  # that we wish to use
  llvm_map_components_to_libnames(llvm_libs bitwriter core irreader linker
                                            support)
endif()

# --------------------------------------------------
# llvm2c
# --------------------------------------------------
set(llvm2c_sources
  main.cpp

  core/Block.cpp
  core/Block.h
  core/Func.cpp
  core/Func.h
  core/Program.cpp
  core/Program.h

  expr/BinaryExpr.cpp
  expr/BinaryExpr.h
  expr/Expr.cpp
  expr/Expr.h
  expr/UnaryExpr.cpp
  expr/UnaryExpr.h

  parser/ProgramParser.cpp
  parser/ProgramParser.h
  parser/SimplifyingExprVisitor.cpp
  parser/addSignCasts.cpp
  parser/arrowify.cpp
  parser/cfunc.h
  parser/computeGlobalVarsOrder.cpp
  parser/constval.cpp
  parser/createAllocas.cpp
  parser/createBlocks.cpp
  parser/createConstants.cpp
  parser/createExpressions.cpp
  parser/createFunctionParameters.cpp
  parser/createFunctions.cpp
  parser/deleteRedundantCasts.cpp
  parser/deleteUnusedVariables.cpp
  parser/determineIncludes.cpp
  parser/extractVars.cpp
  parser/findDeclaredFunctions.cpp
  parser/findMetadataFunctionNames.cpp
  parser/findMetadataVariableNames.cpp
  parser/fixMainParameters.cpp
  parser/identifyInlinableBlocks.cpp
  parser/initializeGlobalVars.cpp
  parser/inlineBlocks.cpp
  parser/memcpyToAssign.cpp
  parser/nameFunctions.cpp
  parser/parseBreaks.cpp
  parser/parseMetadataTypes.cpp
  parser/parseStructDeclarations.cpp
  parser/parseStructItems.cpp
  parser/passes.h
  parser/prepareBitcastUnion.cpp
  parser/refDeref.cpp
  parser/toinst.cpp
  parser/toinst.h

  type/Type.cpp
  type/Type.h
  type/TypeHandler.cpp
  type/TypeHandler.h

  writer/CWriter.cpp
  writer/ExprWriter.cpp
  writer/Writer.cpp
)
add_executable(llvm2c ${llvm2c_sources})
target_link_libraries(llvm2c ${llvm_libs})

include(GNUInstallDirs)
install(TARGETS llvm2c
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# --------------------------------------------------
# Subdirectories
# --------------------------------------------------
add_subdirectory(test)
