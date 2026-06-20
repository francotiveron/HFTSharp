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
set(BINDINGS_OUTPUT_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/shared/generated")
set(BINDINGS_OUTPUT_FILE "${BINDINGS_OUTPUT_DIR}/HftShm.g.cs")

# ---------------------------------------------------------------------------
# Binding regeneration is done manually via regen-bindings.sh.
# CMake does not support '#' in custom target paths, and this project
# directory contains '#'. The generated file is committed to source control
# and only needs refreshing when shared/hft_shm.h changes.
# ---------------------------------------------------------------------------
message(STATUS "To regenerate C# bindings, run: ./regen-bindings.sh")
