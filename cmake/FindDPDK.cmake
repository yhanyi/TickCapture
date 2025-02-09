include(FindPackageHandleStandardArgs)

find_path(DPDK_INCLUDE_DIR rte_config.h
    PATHS ENV RTE_SDK
    PATH_SUFFIXES include
)

find_library(DPDK_LIBRARY
    NAMES dpdk
    PATHS ENV RTE_SDK
    PATH_SUFFIXES lib
)

find_package_handle_standard_args(DPDK
    REQUIRED_VARS DPDK_LIBRARY DPDK_INCLUDE_DIR
)

if(DPDK_FOUND AND NOT TARGET DPDK::DPDK)
    add_library(DPDK::DPDK INTERFACE IMPORTED)
    set_target_properties(DPDK::DPDK PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${DPDK_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${DPDK_LIBRARY}"
    )
endif()
