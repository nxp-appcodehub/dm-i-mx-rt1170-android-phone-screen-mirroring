# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(scrmirror)

set(JPEGDEC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../JPEGDEC/src)

zephyr_library_include_directories(${JPEGDEC_DIR})

FILE(GLOB app_sources src/*.c ${JPEGDEC_DIR}/jpeg.c)
target_sources(app PRIVATE ${app_sources})

if (CONFIG_NET_STACK_CODE_RELOCATE)
  # Relocate key networking stack components and L2 layer to RAM
  zephyr_code_relocate(LIBRARY subsys__net__ip
    LOCATION "${CONFIG_NET_STACK_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(LIBRARY subsys__net
    LOCATION "${CONFIG_NET_STACK_CODE_RAM_NAME}_TEXT" NOKEEP)
if (CONFIG_NET_L2_ETHERNET)
  zephyr_code_relocate(LIBRARY drivers__ethernet
    LOCATION "${CONFIG_NET_STACK_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(LIBRARY subsys__net__l2__ethernet
    LOCATION "${CONFIG_NET_STACK_CODE_RAM_NAME}_TEXT" NOKEEP)
endif()
endif()
