add_library(tick_capture
    capture/packet_capture.cpp
    storage/tick_storage.cpp
    network/coordinator.cpp
    node/capture_node.cpp
)

target_include_directories(tick_capture
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(tick_capture
    PUBLIC
        Boost::system
        TBB::tbb
        libzmq
        fmt::fmt
)

if(TICK_CAPTURE_USE_DPDK)
    target_compile_definitions(tick_capture PRIVATE USE_DPDK)
    target_link_libraries(tick_capture PUBLIC DPDK::DPDK)
endif()
