idf_component_register(
    SRCS "src/error.cpp" "src/sched.cpp" "src/system.cpp" "src/app.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_system esp_hw_support
    PRIV_REQUIRES esp_app_format
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
