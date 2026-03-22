idf_component_register(
    SRCS "src/netif.cpp" "src/types.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_netif esp_event
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
