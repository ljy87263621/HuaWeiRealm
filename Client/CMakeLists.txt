cmake_minimum_required(VERSION 3.10)
project(lvgl C)

option(LV_USE_DRAW_SDL "Use SDL draw unit" OFF)
option(LV_USE_LIBPNG "Use libpng to decode PNG" OFF)
option(LV_USE_LIBJPEG_TURBO "Use libjpeg turbo to decode JPEG" OFF)
option(LV_USE_FFMPEG "Use libffmpeg to display video using lv_ffmpeg" OFF)
option(LV_USE_FREETYPE "Use freetype lib" OFF)
option(CHAT_BOARD_FBDEV "Build for a Linux framebuffer and evdev touch screen" OFF)

set(CMAKE_C_STANDARD 99)#C99 # lvgl officially support C99 and above
set(CMAKE_CXX_STANDARD 17)#C17
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

# Build the simulator UI sources. ChatRoom.c is a separate Linux socket
# console client with its own main(), so it is intentionally excluded here.
include_directories(${PROJECT_SOURCE_DIR}/mycode)
file(GLOB_RECURSE MY_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/mycode/Class/*.c
    ${PROJECT_SOURCE_DIR}/mycode/squareline_ui/*.c
    ${PROJECT_SOURCE_DIR}/mycode/chat_frontend.c
    ${PROJECT_SOURCE_DIR}/mycode/chat_network.c
    ${PROJECT_SOURCE_DIR}/mycode/chat_transfer.c
    ${PROJECT_SOURCE_DIR}/mycode/chat_login_ui.c)
list(REMOVE_ITEM MY_SOURCES ${PROJECT_SOURCE_DIR}/mycode/Class/06-img.c)

if(WIN32 AND NOT CHAT_BOARD_FBDEV)
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${PROJECT_SOURCE_DIR}/third_party/SDL2-2.28.5/cmake")
endif()

if(NOT CHAT_BOARD_FBDEV)
    find_package(SDL2 REQUIRED SDL2)
else()
    add_compile_definitions(CHAT_BOARD_FBDEV=1)
endif()
add_compile_definitions($<$<BOOL:${LV_USE_DRAW_SDL}>:LV_USE_DRAW_SDL=1>)
add_compile_definitions($<$<BOOL:${LV_USE_LIBPNG}>:LV_USE_LIBPNG=1>)
add_compile_definitions($<$<BOOL:${LV_USE_LIBJPEG_TURBO}>:LV_USE_LIBJPEG_TURBO=1>)
add_compile_definitions($<$<BOOL:${LV_USE_FFMPEG}>:LV_USE_FFMPEG=1>)

add_subdirectory(third_party/lvgl)
# LVGL reads lv_conf.h from the application root, while application sources
# include it as "lvgl/lvgl.h".  Keep both lookup roots after vendoring LVGL.
target_include_directories(lvgl PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_party)
if(NOT CHAT_BOARD_FBDEV)
    target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
endif()

add_executable(main main.c mouse_cursor_icon.c ${MY_SOURCES})

target_compile_definitions(main PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(main lvgl m pthread)
if(NOT CHAT_BOARD_FBDEV)
    target_link_libraries(main ${SDL2_LIBRARIES})
endif()
add_custom_target (run COMMAND ${EXECUTABLE_OUTPUT_PATH}/main DEPENDS main)

if(LV_USE_DRAW_SDL)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
    # Need to install libsdl2-image-dev
    # `sudo apt install libsdl2-image-dev`
    # `brew install sdl2_image`
    find_package(SDL2_image REQUIRED)
    target_include_directories(lvgl PUBLIC ${SDL2_IMAGE_INCLUDE_DIRS})
    target_link_libraries(main ${SDL2_IMAGE_LIBRARIES})
endif(LV_USE_DRAW_SDL)

if(LV_USE_LIBPNG)
    find_package(PNG REQUIRED)
    target_include_directories(lvgl PUBLIC ${PNG_INCLUDE_DIR})
    target_link_libraries(main ${PNG_LIBRARY})
endif(LV_USE_LIBPNG)

if(LV_USE_LIBJPEG_TURBO)
    # Need to install libjpeg-turbo8-dev
    # `sudo apt install libjpeg-turbo8-dev`
    # `brew install libjpeg-turbo`
    find_package(JPEG REQUIRED)
    target_include_directories(lvgl PUBLIC ${JPEG_INCLUDE_DIRS})
    target_link_libraries(main ${JPEG_LIBRARIES})
endif(LV_USE_LIBJPEG_TURBO)

if(LV_USE_FFMPEG)
    target_link_libraries(main avformat avcodec avutil swscale)
endif(LV_USE_FFMPEG)

if(LV_USE_FREETYPE)
    find_package(Freetype REQUIRED)
    target_link_libraries(main ${FREETYPE_LIBRARIES})
    target_include_directories(lvgl PUBLIC ${FREETYPE_INCLUDE_DIRS})
endif(LV_USE_FREETYPE)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(lvgl PRIVATE
        -pedantic-errors
        -Wall
        -Wclobbered
        -Wdeprecated
        -Wdouble-promotion
        -Wempty-body
        -Wextra
        -Wformat-security
        -Wmaybe-uninitialized
        # -Wmissing-prototypes
        -Wpointer-arith
        -Wmultichar
        -Wno-pedantic # ignored for now, we convert functions to pointers for propertis table.
        -Wreturn-type
        -Wshadow
        -Wshift-negative-value
        -Wsizeof-pointer-memaccess
        -Wtype-limits
        -Wundef
        -Wuninitialized
        -Wunreachable-code
        -Wfloat-conversion
        -Wstrict-aliasing
    )

    # The current MinGW/UCRT64 toolchain does not ship the sanitizer runtimes.
    # Keep Debug builds linkable there while retaining sanitizers on supported hosts.
    if(NOT MINGW)
        target_compile_options(main PRIVATE -fsanitize=address,leak,undefined)
        target_link_options(main PRIVATE -fsanitize=address,leak,undefined)
    else()
        message(STATUS "Sanitizers are disabled for MinGW because their runtimes are unavailable")
    endif()
endif()
