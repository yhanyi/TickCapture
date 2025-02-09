add_executable(capture_service
    capture_service.cpp
)

target_link_libraries(capture_service
    PRIVATE
        tick_capture
        Boost::program_options
)
