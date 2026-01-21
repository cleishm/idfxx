idf_component_register(
    SRCS "src/nvs.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
