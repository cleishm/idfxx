idf_component_register(
    SRCS
        "src/error.cpp"
        "src/endpoint.cpp"
        "src/ip_addr.cpp"
        "src/sockaddr.cpp"
        "src/sockopt.cpp"
        "src/select.cpp"
        "src/ip_socket_base.cpp"
        "src/ip_io_socket_base.cpp"
        "src/connectionless_socket_base.cpp"
        "src/stream_socket.cpp"
        "src/datagram_socket.cpp"
        "src/raw_socket.cpp"
        "src/listener.cpp"
        "src/resolver.cpp"
        "src/netconn_base.cpp"
        "src/netconn_connectionless_base.cpp"
        "src/netconn_stream.cpp"
        "src/netconn_datagram.cpp"
        "src/netconn_raw.cpp"
        "src/netconn_listener.cpp"
        "src/netconn_buffer.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES lwip vfs
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

# Register test sources for the central test app
file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
if(_test_sources)
    set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
endif()
