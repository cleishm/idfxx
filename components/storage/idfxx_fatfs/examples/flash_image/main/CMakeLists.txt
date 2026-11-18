idf_component_register(SRCS "main.cpp" INCLUDE_DIRS ".")

# Builds a FAT image of ../data, sized to fit the `assets` partition, and flashes it there with `idf.py flash`.
fatfs_create_rawflash_image(assets ../data FLASH_IN_PROJECT)
