cmake_minimum_required(VERSION 3.20)
project(hft_demo LANGUAGES C CXX)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Shared library
# Consumed by the C++ executor (link) and the F# commander (P/Invoke).
# ---------------------------------------------------------------------------
add_library(hft_shm SHARED shared/hft_shm.c)
target_include_directories(hft_shm PUBLIC shared)
target_link_libraries(hft_shm PRIVATE rt)
target_compile_options(hft_shm PRIVATE -Wall -Wextra)

add_subdirectory(cpp)

# ---------------------------------------------------------------------------
# C# binding generation via ClangSharpPInvokeGenerator
#
# Install the tool:  dotnet tool install -g ClangSharpPInvokeGenerator
# Run the target:    cmake --build <build-dir> --target generate_bindings
#
# The generated file is committed to source control so the build does not
# require the generator to be present on every machine.
# ---------------------------------------------------------------------------
set(BINDINGS_HEADER      "${CMAKE_CURRENT_SOURCE_DIR}/shared/hft_shm.h")
set(BINDINGS_OUTPUT_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/shared/generated")
set(BINDINGS_OUTPUT_FILE "${BINDINGS_OUTPUT_DIR}/HftShm.g.cs")

# ClangSharp ships libclang.so alongside the tool; find it so the generator
# can load it at runtime.  Override with -DLIBCLANG_DIR=... if your install
# path differs.
if(NOT DEFINED LIBCLANG_DIR)
    set(LIBCLANG_DIR
        "$ENV{HOME}/.dotnet/tools/.store/clangsharppinvokegenerator/21.1.8.3/clangsharppinvokegenerator.linux-x64/21.1.8.3/tools/any/linux-x64"
    )
endif()

add_custom_command(
    OUTPUT  "${BINDINGS_OUTPUT_FILE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINDINGS_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E env
                "LD_LIBRARY_PATH=${LIBCLANG_DIR}:$ENV{LD_LIBRARY_PATH}"
            ClangSharpPInvokeGenerator
                --file            "${BINDINGS_HEADER}"
                --namespace       HftDemo.Interface
                --output          "${BINDINGS_OUTPUT_FILE}"
                --methodClassName HftNative
                --libraryPath     hft_shm
                --config          unix-types
                -I /usr/lib/llvm-18/lib/clang/18/include
                -I /usr/include
                -I /usr/include/x86_64-linux-gnu
    DEPENDS "${BINDINGS_HEADER}"
    COMMENT "Regenerating C# P/Invoke bindings from hft_shm.h"
    VERBATIM
)

add_custom_target(generate_bindings
    DEPENDS "${BINDINGS_OUTPUT_FILE}"
)

message(STATUS "C# binding target available: cmake --build <build-dir> --target generate_bindings")
