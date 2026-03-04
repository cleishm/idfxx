idf_component_register(
    SRCS "src/client.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
