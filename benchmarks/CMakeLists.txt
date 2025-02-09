# Benchmark executable
add_executable(tick_capture_benchmark
    market_data_simulator.cpp
    benchmark_main.cpp
)

target_link_libraries(tick_capture_benchmark
    PRIVATE
        tick_capture
        Boost::program_options
        fmt::fmt
        TBB::tbb
)

# Add include directories
target_include_directories(tick_capture_benchmark
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)
