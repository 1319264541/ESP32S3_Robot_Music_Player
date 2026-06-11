list(APPEND srcs    bmp.c
                    gif.c
                    jpeg.c
                    piclib.c
                    png.c
                    pngle.c
                    tjpgd.c
                    )

idf_component_register(SRCS "piclib.c" "pngle.c" "jpeg.c" "bmp.c" "${srcs}"
                    INCLUDE_DIRS "."
                    REQUIRES BSP
                             MYFATFS
                             esp_driver_jpeg
                             fatfs
                             )