set(SOURCES 
    "Application.cpp"
    "audio/TLV320.cpp"
    "audio/TLV320.h"
    "codec/FreeDVTask.cpp"
    "codec/FreeDVTask.h"
    "radio/icom/IcomRadioTask.cpp"
    "radio/icom/PacketTypes.cpp"
    "radio/icom/ProtocolStateMachine.cpp"
    "ui/UserInterfaceTask.cpp"
    "ui/UserInterfaceTask.h"
    "util/NamedQueue.cpp"
    "util/NamedQueue.h")

if(${ESP_PLATFORM})
    idf_component_register(SRCS ${SOURCES}
                            INCLUDE_DIRS
                                ${CMAKE_CURRENT_LIST_DIR}
                                $ENV{IDF_PATH}/components
                            #LDFRAGMENTS
                            #    "codec2.lf"
                            REQUIRES
                                smooth_component)
endif()

# Compiles and links Codec2 into the application.
# ==================================================================
include(FetchContent)
message("Setting up Codec2...")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=gnu11 -ffunction-sections -fdata-sections -O3 -fsingle-precision-constant -Wdouble-promotion")

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
    GIT_TAG origin/ms-embedded-noarm
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

if(${ESP_PLATFORM})
    target_link_libraries(${COMPONENT_LIB} PUBLIC codec2)
else()
    project(sm1000neo.elf)
    add_executable(sm1000neo ${SOURCES})
    target_link_libraries(sm1000neo smooth pthread codec2)
    target_include_directories(sm1000neo PRIVATE ${CMAKE_CURRENT_LIST_DIR})
endif()
