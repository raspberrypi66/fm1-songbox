# All .c files in these folders. ui/assets holds the generated image arrays
# (tools/ui_assets/make_assets.py), so new assets need no edit here.
idf_component_register(SRC_DIRS "." "ui" "ui/assets"
                       INCLUDE_DIRS "." "ui")

# Generated image arrays (LVGLImage.py) include "lvgl.h" only with this defined
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# The project's songs/ folder as a FAT (wear levelling) image for the "storage"
# partition. Not part of `idf.py flash`, so flashing firmware keeps the songs;
# write it with `idf.py storage-flash` after changing the folder.
fatfs_create_spiflash_image(storage ../songs)
