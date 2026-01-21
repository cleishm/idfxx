idf_component_register(
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
