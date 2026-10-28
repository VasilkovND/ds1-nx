# ds1 - Dead Space (Xbox 360, 2008) on the Nintendo Switch.
#
# The recompiled code is generated from your own default.xex by tools/codegen.sh (ReXGlue's codegen, with
# codegen/names.toml), never committed. Point DS1_CODEGEN_DIR at its output.
# At configure time tools/prepare_tree.py mirrors generated/default/ as symlinks with a Switch PCH
# (tools/make_nx_pch.py); see PORTING.md. tools/build_mac.sh does all of it.
#
# Configure (Switch):
#   cmake -S ds1 -B ds1/out/sw -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=tools/switch/cmake/switch-devkitA64.cmake \
#     -DREXSDK_DIR=$PWD/sdk -DDS1_CODEGEN_DIR=$PWD/ds1/out/codegen -DREXGLUE_SWITCH_DEKO3D=ON
#   cmake --build ds1/out/sw

cmake_minimum_required(VERSION 3.25)
project(ds1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/ds1_switch.cmake)

if(NOT REXGLUE_SWITCH_DEKO3D)
    message(FATAL_ERROR "ds1: only the deko3d build exists (-DREXGLUE_SWITCH_DEKO3D=ON)")
endif()

add_executable(ds1
    src/main.cpp
    src/ds1_gpu.cpp
    src/ds1_shaders.cpp
    src/ds1_cpu.cpp
    src/ds1_menu.cpp
    # The shader library reader, shared with the PC tools (shaders/).
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/ds_shader_library.cpp
)

ds1_setup_target(ds1)
rexglue_switch_add_deko3d(ds1)
target_compile_definitions(ds1 PRIVATE DS1_DEKO3D=1)
# Debug modes (src/ds1_gpu.cpp, from ds2-nx): ZL + ZR shortcuts (frame dump and draw trace, captures, draw-skipping
# modes), telemetria.flag next to the NRO. On while the port is brought up; a release build sets it OFF.
option(DS1_DEPURACION "Debug modes: ZL + ZR shortcuts, telemetria.flag" ON)
target_compile_definitions(ds1 PRIVATE DS1_DEPURACION=$<BOOL:${DS1_DEPURACION}>)
target_include_directories(ds1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders ${REXSDK_DIR}/thirdparty/xxHash)

# Hooks on game functions are compiled only when codegen/names.toml names them (the names exist in the generated code
# only then).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/codegen/names.toml" DS1_NAMES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/codegen/names.toml")
set(DS1_GAME_HOOKS "")
# Each hook in a file is compiled only when its function is named (DS1_HOOK_<name> defined); the file is built when at
# least one of its names is there.
function(ds1_add_hook source)
    set(found "")
    foreach(name IN LISTS ARGN)
        if(DS1_NAMES MATCHES "\nname = \"${name}\"")
            list(APPEND found ${name})
            target_compile_definitions(ds1 PRIVATE DS1_HOOK_${name}=1)
        endif()
    endforeach()
    if(NOT found)
        message(STATUS "ds1: ${source} off (codegen/names.toml names none of ${ARGN})")
        return()
    endif()
    list(APPEND DS1_GAME_HOOKS ${source})
    set(DS1_GAME_HOOKS ${DS1_GAME_HOOKS} PARENT_SCOPE)
    message(STATUS "ds1: ${source} on: ${found}")
endfunction()
ds1_add_hook(src/ds1_shader_dump.cpp ds1_D3DDevice_CreateVertexShader ds1_D3DDevice_CreatePixelShader)
# The XGRegister*Shader hooks in the same file (shaders the game registers in place) need their object size checked
# for this XDK first: only with both names and DS1_XGREGISTER=ON.
option(DS1_XGREGISTER "Hook XGRegister{Vertex,Pixel}Shader (src/ds1_shader_dump.cpp)" OFF)
if(DS1_XGREGISTER AND DS1_NAMES MATCHES "\nname = \"ds1_XGRegisterVertexShader\"" AND
   DS1_NAMES MATCHES "\nname = \"ds1_XGRegisterPixelShader\"")
    target_compile_definitions(ds1 PRIVATE DS1_XGREGISTER=1)
endif()
ds1_add_hook(src/ds1_audio.cpp rw__audio__core__MixWithGain rw__audio__core__CopyWithGain
             ds1_rw_audio_Compressor1_Process ds1_rw_audio_MixerThreadLoop ds1_xaudio_ThreadA ds1_xaudio_ThreadB
             rw__audio__core__StreamPool__AcquireStream)
ds1_add_hook(src/ds1_game_cpu.cpp ds1_SetThreadPriority ds1_D3D_GpuWaitStep ds1_D3DDevice_SetGammaRamp)
ds1_add_hook(src/ds1_resolution.cpp ds1_ConfigureDisplay)
target_sources(ds1 PRIVATE ${DS1_GAME_HOOKS})

# The version (Homebrew Menu and the first lines of the log) and the commit it was built from, so a log a player sends
# says which build made it. The commit is read when CMake configures (build_mac.sh RECONFIGURE=1 refreshes it).
set(DS1_VERSION "0.0.1")
execute_process(COMMAND git rev-parse --short=10 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE DS1_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no --ignore-submodules=all WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE DS1_SUCIO OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT DS1_COMMIT)
    set(DS1_COMMIT "unknown")
elseif(DS1_SUCIO)
    string(APPEND DS1_COMMIT "+changes")
endif()
target_compile_definitions(ds1 PRIVATE DS1_VERSION="${DS1_VERSION}" DS1_COMMIT="${DS1_COMMIT}")

# Nintendo Switch: ds1.nro. On the SD card: sdmc:/switch/ds1-nx/ds1-nx.nro, with default.xex and the .viv files beside it
# (src/ds1_app.h finds them there). No icon from the game: the Homebrew Menu shows the default one.
if(REXGLUE_PLATFORM_SWITCH)
    rexglue_switch_add_nro(ds1
        NAME "Dead Space"
        AUTHOR "VasilkovND"
        VERSION "${DS1_VERSION}")
endif()
