set(src_dirs
            KEY
            MYIIC
            LCD
            LED
            SPI_SD
            MYSPI
            XL9555
            MYI2S
            ES8311
			USART)

set(include_dirs
            KEY
            MYIIC
            LCD
            LED
            SPI_SD
            MYSPI
            XL9555
            MYI2S
            ES8311
			USART)

set(requires
            driver
            fatfs
            esp_lcd)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)