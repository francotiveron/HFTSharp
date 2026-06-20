add_executable(executor executor.cpp)

target_link_libraries(executor PRIVATE hft_shm pthread)

target_compile_options(executor PRIVATE
    -Wall -Wextra
    -msse2          # ensures _mm_pause is available in all build types
    $<$<CONFIG:Release>:-O3 -march=native>
)

set_target_properties(executor PROPERTIES CXX_STANDARD 20)
