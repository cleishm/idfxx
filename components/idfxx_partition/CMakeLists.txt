idf_component_register(
    SRCS "src/partition.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_partition
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
