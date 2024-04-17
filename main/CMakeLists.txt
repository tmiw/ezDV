set(SOURCES 
    "Application.cpp"
    "network/HttpServerTask.cpp"
    "network/NetworkMessage.cpp"
    "network/WirelessTask.cpp"
    "task/DVTask.cpp"
    "task/DVTaskControlMessage.cpp"
    "task/DVTimer.cpp")

if(${ESP_PLATFORM})
    idf_component_register(SRCS ${SOURCES}
                            INCLUDE_DIRS
                                ${CMAKE_CURRENT_LIST_DIR}
                                $ENV{IDF_PATH}/components
                            REQUIRES
                                soc
                                vfs
                                spiffs
                                fatfs
                                wear_levelling
                                esp_http_server
                                esp_netif
                                esp_timer
                                esp_wifi
                                app_update
                                driver
                                nvs_flash
                                json)

endif()

# Generate HTML with correct version number
string(TIMESTAMP PROJECT_YEAR "%Y")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/http_server_files/index.html.in ${CMAKE_CURRENT_SOURCE_DIR}/http_server_files/index.html)

# Embedded HTTP server files
spiffs_create_partition_image(http_0 http_server_files FLASH_IN_PROJECT)
