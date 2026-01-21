idf_component_register(
    SRCS "src/stmpe610.cpp"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
