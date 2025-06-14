# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)


set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(mqtt_client C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1


add_executable(${PROJECT_NAME}  
        pwmControlIOT.c 
        lib/ssd1306.c # Biblioteca para o display OLED
        )



# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 1)

# Add the standard library to the build
target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_adc
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    pico_mbedtls
    pico_lwip_mbedtls
    hardware_pwm
    hardware_i2c
    )

# Add the standard include files to the build
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(${PROJECT_NAME} 
        
        )

pico_add_extra_outputs(${PROJECT_NAME})

#Converte o .pio para .h
pico_generate_pio_header(${PROJECT_NAME}  ${CMAKE_CURRENT_LIST_DIR}/ws2818b.pio)

target_sources(${PROJECT_NAME} PRIVATE pwmControlIOT.c)
