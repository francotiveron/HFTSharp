add_executable(executor executor.cpp)

target_link_libraries(executor PRIVATE hft_shm pthread)

target_compile_options(executor PRIVATE
    -Wall -Wextra
    -msse2          # ensures _mm_pause is available in all build types
    $<$<CONFIG:Release>:-O3 -march=native>
)

# Pass -DHFT_VERBOSE=1 to cmake to enable hot-path logging (disabled in Release).
option(HFT_VERBOSE "Enable hot-path logging in executor" OFF)
if(HFT_VERBOSE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(executor PRIVATE HFT_VERBOSE)
endif()

set_target_properties(executor PROPERTIES CXX_STANDARD 20)
