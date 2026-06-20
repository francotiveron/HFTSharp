add_executable(executor executor.cpp)

target_link_libraries(executor PRIVATE hft_shm)

target_compile_options(executor PRIVATE
    -Wall -Wextra
    $<$<CONFIG:Release>:-O3 -march=native>
)
