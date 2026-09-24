cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(LinuxOnVita)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# Display name under bubble in LiveArea
set(VITA_APP_NAME "LinuxOnVita")

# Title ID for the application (9 uppercase alphanumeric characters)
set(VITA_TITLEID  "LNXONVITA")

set(VITA_VERSION  "01.10")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2")

set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")

# Add all files needed to compile
add_executable(${PROJECT_NAME}
  main.c
  debugScreenFont.c
)

target_link_libraries(${PROJECT_NAME}
  SceCtrl_stub
  taihen_stub
  SceDisplay_stub
  SceVshBridge_stub
)

# Create Vita SELF
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

# Resources for VPK packaging
set(VPK_RESOURCES
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png" "sce_sys/icon0.png"
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg.png" "sce_sys/livearea/contents/bg.png"
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png" "sce_sys/livearea/contents/startup.png"
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml" "sce_sys/livearea/contents/template.xml"
)

# If LINUX_FILES_DIR is provided, bundle the kernel, loaders, DTBs, and config template
if(DEFINED LINUX_FILES_DIR AND EXISTS "${LINUX_FILES_DIR}")
  message(STATUS "Configuring bundled Linux files from: ${LINUX_FILES_DIR}")
  set(PAYLOAD_LIST
    "baremetal-loader.skprx"
    "baremetal-loader_360.skprx"
    "payload.bin"
    "zImage"
    "vita.dtb"
    "vita1000.dtb"
    "vita2000.dtb"
    "pstv.dtb"
    "wpa_supplicant.conf"
  )
  foreach(item ${PAYLOAD_LIST})
    if(EXISTS "${LINUX_FILES_DIR}/${item}")
      message(STATUS "  + Bundling: ${item}")
      list(APPEND VPK_RESOURCES FILE "${LINUX_FILES_DIR}/${item}" "data/${item}")
    else()
      message(WARNING "  - Optional payload file not found: ${LINUX_FILES_DIR}/${item}")
    endif()
  endforeach()
endif()

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  ${VPK_RESOURCES}
)
