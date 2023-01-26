set(SOURCES 
    "Application.cpp"
    "audio/AudioInput.cpp"
    "audio/AudioMixer.cpp"
    "audio/BeeperMessage.cpp"
    "audio/BeeperTask.cpp"
    "audio/FreeDVMessage.cpp"
    "audio/FreeDVTask.cpp"
    "audio/VoiceKeyerMessage.cpp"
    "audio/VoiceKeyerTask.cpp"
    "audio/WAVFileReader.cpp"
    "driver/BatteryMessage.cpp"
    "driver/ButtonArray.cpp"
    "driver/ButtonMessage.cpp"
    "driver/InputGPIO.cpp"
    "driver/I2CDevice.cpp"
    "driver/LedArray.cpp"
    "driver/LedMessage.cpp"
    "driver/MAX17048.cpp"
    "driver/OutputGPIO.cpp"
    "driver/TLV320.cpp"
    "driver/TLV320Message.cpp"
    "network/icom/AudioState.cpp"
    "network/icom/AreYouReadyAudioState.cpp"
    "network/icom/AreYouReadyCIVState.cpp"
    "network/icom/AreYouReadyControlState.cpp"
    "network/icom/AreYouReadyState.cpp"
    "network/icom/AreYouThereState.cpp"
    "network/icom/CIVState.cpp"
    "network/icom/IcomAudioStateMachine.cpp"
    "network/icom/IcomCIVStateMachine.cpp"
    "network/icom/IcomControlStateMachine.cpp"
    "network/icom/IcomMessage.cpp"
    "network/icom/IcomPacket.cpp"
    "network/icom/IcomProtocolState.cpp"
    "network/icom/IcomSocketTask.cpp"
    "network/icom/IcomStateMachine.cpp"
    "network/icom/LoginState.cpp"
    "network/icom/StateMachine.cpp"
    "network/icom/TrackedPacketState.cpp"
    "network/HttpServerTask.cpp"
    "network/NetworkMessage.cpp"
    "network/WirelessTask.cpp"
    "storage/SettingsMessage.cpp"
    "storage/SettingsTask.cpp"
    "task/DVTask.cpp"
    "task/DVTaskControlMessage.cpp"
    "task/DVTimer.cpp"
    "ui/RFComplianceTestTask.cpp"
    "ui/UserInterfaceTask.cpp")

if(${ESP_PLATFORM})
    idf_component_register(SRCS ${SOURCES}
                            INCLUDE_DIRS
                                ${CMAKE_CURRENT_LIST_DIR}
                                $ENV{IDF_PATH}/components
                            #LDFRAGMENTS
                            #    "codec2.lf"
                            REQUIRES
                                esp-dsp
                                ulp
                                soc
                                vfs
                                spiffs
                                fatfs
                                wear_levelling
                                esp_http_server
                                esp_timer
                                esp_wifi
                                driver
                                nvs_flash
                                json)

endif()

# Compiles and links Codec2 into the application.
# ==================================================================
include(FetchContent)
message("Setting up Codec2...")

set(UNITTEST OFF)
set(BUILD_SHARED_LIBS OFF)
set(MICROCONTROLLER_BUILD ON)

add_definitions(-D__EMBEDDED__ -D__REAL__)
add_definitions(-DFREEDV_MODE_EN_DEFAULT=0 -DFREEDV_MODE_1600_EN=1 -DFREEDV_MODE_700D_EN=1 -DFREEDV_MODE_700E_EN=1 -DCODEC2_MODE_EN_DEFAULT=0 -DCODEC2_MODE_1300_EN=1 -DCODEC2_MODE_700C_EN=1)
                    
# Disable export and install; codec2 tries to do this and ESP-IDF's build system doesn't support it.
function(install)
endfunction()

function(export)
endfunction()

function(export)
endfunction()

FetchContent_Declare(codec2
    GIT_REPOSITORY https://github.com/drowe67/codec2.git
    GIT_TAG origin/master
    GIT_SHALLOW ON
    GIT_PROGRESS ON
)
FetchContent_GetProperties(codec2)
if(NOT ${codec2_POPULATED})
    FetchContent_Populate(codec2)
endif()
set(CMAKE_REQUIRED_FLAGS "")

set(LPCNET OFF CACHE BOOL "")
add_subdirectory(${codec2_SOURCE_DIR} ${codec2_BINARY_DIR} EXCLUDE_FROM_ALL)

target_link_libraries(${COMPONENT_LIB} PUBLIC codec2)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=gnu11 -ffunction-sections -fdata-sections -O3 -fsingle-precision-constant -Wdouble-promotion")
target_compile_options(codec2 PRIVATE -Wall -ffunction-sections -fdata-sections -O3 -fsingle-precision-constant -Wdouble-promotion)

# RISC-V ULP setup 
ulp_embed_binary(ulp_main "ulp/main.c" "ulp/main.c")

# Embedded HTTP server files
spiffs_create_partition_image(http http_server_files FLASH_IN_PROJECT)
