# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.c* ${CMAKE_SOURCE_DIR}/main/ex_i2c/*.c* ${CMAKE_SOURCE_DIR}/main/in_i2c/*.c* )

idf_component_register(SRCS ${app_sources}
                       REQUIRES M5GFX M5Unified ArduinoJson esp_http_server esp-tls esp_https_ota app_update esp_app_format esp_partition esp_psram arduino-esp32
                       PRIV_REQUIRES lwip efuse spi_flash
                       INCLUDE_DIRS ""
                       )

add_prebuilt_library(kantan-music_a "kantan-music/esp32/libkantan-music.a")

target_link_libraries(${COMPONENT_LIB} PRIVATE kantan-music_a)

