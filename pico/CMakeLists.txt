cmake_minimum_required(VERSION 3.12)

set(PROGRAM_NAME ney_tack_device)
set(PICO_BOARD pico_w)

# Pull in PICO SDK (must be before project)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(ney_tack_device C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_EXAMPLES_PATH ${PROJECT_SOURCE_DIR})

# Initialize the SDK
pico_sdk_init()

add_executable(blink
  blink.c
)

add_executable(ney_tack
  ney_tack.c
)

# Define a variable for the libraries that both blink and ney_tack need to link against
set(COMMON_LIBS
  pico_stdlib # for core functionality
  pico_cyw43_arch_none # we need Wifi to access the GPIO, but we don't need anything else
)

# Link blink with the common libraries
target_link_libraries(blink ${COMMON_LIBS})

# Link ney_tack with the common libraries
target_link_libraries(ney_tack
  ${COMMON_LIBS}
  hardware_i2c
  pico_btstack_ble # provides Bluetooth Low Energy (BLE) functionality
  pico_btstack_cyw43 # provides support for the Cypress CYW4343W Wi-Fi and Bluetooth combo chip
)
target_include_directories(ney_tack PRIVATE
  ${CMAKE_CURRENT_LIST_DIR} # For btstack config
)

# Add the GATT database to the ney_tack target (will create mygatt.h with profile_data variable)
pico_btstack_make_gatt_header(ney_tack PRIVATE "${CMAKE_CURRENT_LIST_DIR}/mygatt.gatt")

# create map/bin/hex file etc.
pico_add_extra_outputs(blink)
pico_add_extra_outputs(ney_tack)

# Enable usb output, disable uart output
pico_enable_stdio_usb(ney_tack 0)
pico_enable_stdio_uart(ney_tack 1)