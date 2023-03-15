# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 17)

if(${ESP_PLATFORM})
    
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Include ESP-DSP as a component
set(EXTRA_COMPONENT_DIRS
         externals/esp-dsp)

project(ezdv)
add_subdirectory(externals)    

else()

project(ezdv)

add_subdirectory(externals)    
add_subdirectory(main)

endif()
