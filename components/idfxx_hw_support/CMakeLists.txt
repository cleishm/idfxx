idf_component_register(
    SRCS "src/chip.cpp" "src/mac.cpp"
    INCLUDE_DIRS "include"
    REQUIRES idfxx_core esp_hw_support
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
