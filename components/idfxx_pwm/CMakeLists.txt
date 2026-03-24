idf_component_register(
    SRCS "src/pwm.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_ledc
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
