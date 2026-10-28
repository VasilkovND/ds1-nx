# ds1_switch.cmake - what the codegen-emitted rexglue.cmake and dll_targets.cmake give a title, for the Switch build.
#
#   DS1_CODEGEN_DIR   tools/codegen.sh's output: generated/default/, made from your default.xex
#   REXSDK_DIR        this repository's sdk/ (rexglue-sdk-nx)
#
# The generated trees are used as they are, with one exception: their PCH. Their files include "ds1_pch.h" with
# quotes, which finds the copy next to them first, so the build compiles mirrors of symlinks (tools/prepare_tree.py)
# whose PCH is the Switch one (tools/make_nx_pch.py).

set(DS1_CODEGEN_DIR "" CACHE PATH "tools/codegen.sh output folder (holds generated/default/)")
foreach(_ds1_m default)
    if(NOT EXISTS "${DS1_CODEGEN_DIR}/generated/${_ds1_m}/sources.cmake")
        message(FATAL_ERROR
            "DS1_CODEGEN_DIR must point at tools/codegen.sh's output "
            "(no generated/${_ds1_m}/sources.cmake under '${DS1_CODEGEN_DIR}')")
    endif()
endforeach()

set(REXSDK_DIR "" CACHE PATH "Path to the rexglue-sdk(-nx) source tree")
if(NOT REXSDK_DIR)
    get_filename_component(REXSDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../sdk" ABSOLUTE)
endif()
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
message(STATUS "ds1: ReXGlue SDK from ${REXSDK_DIR}")

# The mirrors of the generated trees with the Switch PCH.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(DS1_TREE "${CMAKE_BINARY_DIR}/generated_nx/default")
foreach(_ds1_m default)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_tree.py"
                "${DS1_CODEGEN_DIR}/generated/${_ds1_m}" "${CMAKE_BINARY_DIR}/generated_nx/${_ds1_m}"
        RESULT_VARIABLE _ds1_tree_rc)
    if(NOT _ds1_tree_rc EQUAL 0)
        message(FATAL_ERROR "ds1: tools/prepare_tree.py failed (${_ds1_tree_rc})")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${DS1_CODEGEN_DIR}/generated/${_ds1_m}/ds1_pch.h"
        "${DS1_CODEGEN_DIR}/generated/${_ds1_m}/sources.cmake")
endforeach()

set(REXGLUE_RECOMP_DEBUG_INFO "none" CACHE STRING
    "Debug info level for generated code: line-tables-only, full, or none")
set(REXGLUE_RECOMP_OPTIONS "")
if(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "none")
    list(APPEND REXGLUE_RECOMP_OPTIONS $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-g0>)
elseif(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "line-tables-only")
    list(APPEND REXGLUE_RECOMP_OPTIONS $<$<CXX_COMPILER_ID:Clang,AppleClang>:-gline-tables-only>)
endif()

# sources.cmake: the support units and the recompiled ones. CMAKE_CURRENT_LIST_DIR there is the mirror.
include("${DS1_TREE}/sources.cmake")
set(DS1_GUEST_SOURCES ${GENERATED_SOURCES})
unset(GENERATED_SOURCES)
set_source_files_properties(${DS1_GUEST_SOURCES} PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")
# The registration unit is one function of ~55,000 SetFunction calls, run once at startup: at -O3 GCC spends
# ~6 minutes on it alone, on one core, longer than all the other units together on ten. -O1 takes seconds. It needs
# nothing from the PCH (its own includes), and a PCH built at -O3 cannot be used at -O1.
set_source_files_properties("${DS1_TREE}/ds1_register.cpp" PROPERTIES
    COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS};-O1"
    SKIP_PRECOMPILE_HEADERS ON)
list(LENGTH DS1_GUEST_SOURCES _ds1_n)
message(STATUS "ds1: ${_ds1_n} recompiled sources for default.xex")

# Usage: ds1_setup_target(<target>): the title, with default.xex's code.
macro(ds1_setup_target target_name)
    add_library(${target_name}_recomp OBJECT ${DS1_GUEST_SOURCES})
    target_include_directories(${target_name}_recomp PRIVATE "${DS1_TREE}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target_name}_recomp PRIVATE rex::runtime)
    rexglue_apply_target_settings(${target_name}_recomp)
    target_precompile_headers(${target_name}_recomp PRIVATE "${DS1_TREE}/ds1_pch.h")
    target_compile_options(${target_name}_recomp PRIVATE ${REXGLUE_RECOMP_OPTIONS})
    target_link_libraries(${target_name} PRIVATE ${target_name}_recomp)
    target_include_directories(${target_name} PRIVATE "${DS1_TREE}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target_name} PRIVATE rex::runtime)
    rexglue_configure_target(${target_name} ${ARGN})
endmacro()
