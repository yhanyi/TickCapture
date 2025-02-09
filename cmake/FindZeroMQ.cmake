include(FindPackageHandleStandardArgs)

# Try to find ZeroMQ includes
find_path(ZeroMQ_INCLUDE_DIR
    NAMES zmq.h
    PATHS
        /usr/local/include
        /opt/local/include
        /opt/homebrew/include
)

# Try to find ZeroMQ libraries
find_library(ZeroMQ_LIBRARY
    NAMES zmq libzmq
    PATHS
        /usr/local/lib
        /opt/local/lib
        /opt/homebrew/lib
)

# Handle the QUIETLY and REQUIRED arguments
find_package_handle_standard_args(ZeroMQ
    DEFAULT_MSG
    ZeroMQ_LIBRARY
    ZeroMQ_INCLUDE_DIR
)

# Create an imported target
if(ZeroMQ_FOUND AND NOT TARGET libzmq)
    add_library(libzmq INTERFACE IMPORTED)
    set_target_properties(libzmq PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ZeroMQ_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${ZeroMQ_LIBRARY}"
    )
endif()

mark_as_advanced(ZeroMQ_INCLUDE_DIR ZeroMQ_LIBRARY)
