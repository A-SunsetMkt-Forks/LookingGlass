cmake_minimum_required(VERSION 3.10)
project(looking-glass-client C CXX)

get_filename_component(PROJECT_TOP "${PROJECT_SOURCE_DIR}/.." ABSOLUTE)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR
    "\n"
    "In-source builds are not supported\n"
    "See build instructions provided in: "
    "${PROJECT_TOP}/doc/build.rst\n"
    "Refusing to continue"
  )
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_TOP}/cmake/" "${PROJECT_SOURCE_DIR}/cmake/")

include(CheckSubmodule)
include(GNUInstallDirs)
include(CheckCCompilerFlag)
include(FeatureSummary)

set(OPTIMIZE_FOR_NATIVE_DEFAULT ON)
include(OptimizeForNative) # option(OPTIMIZE_FOR_NATIVE)
include(UninstallTarget)

find_package(PkgConfig)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

option(ENABLE_OPENGL "Enable the OpenGL renderer"       ON)
add_feature_info(ENABLE_OPENGL ENABLE_OPENGL "Legacy OpenGL renderer.")

option(ENABLE_EGL    "Enable the EGL renderer"          ON)
add_feature_info(ENABLE_EGL ENABLE_EGL "EGL renderer.")

option(ENABLE_BACKTRACE "Enable backtrace support on crash" ON)
add_feature_info(ENABLE_BACKTRACE ENABLE_BACKTRACE "Backtrace support.")

option(ENABLE_ASAN "Build with AddressSanitizer" OFF)
add_feature_info(ENABLE_ASAN ENABLE_ASAN "AddressSanitizer support.")

option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
add_feature_info(ENABLE_UBSAN ENABLE_UBSAN "UndefinedBehaviorSanitizer support.")

option(ENABLE_PIPEWIRE "Build with PipeWire audio output support" ON)
add_feature_info(ENABLE_PIPEWIRE ENABLE_PIPEWIRE "PipeWire audio support.")

option(ENABLE_PULSEAUDIO "Build with PulseAudio audio output support" ON)
add_feature_info(ENABLE_PULSEAUDIO ENABLE_PULSEAUDIO "PulseAudio audio support.")

add_compile_options(
  "-Wall"
  "-Wextra"
  "-Wno-sign-compare"
  "-Wno-unused-parameter"
  "$<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>"
  "$<$<C_COMPILER_ID:GNU>:-Wimplicit-fallthrough=2>"
  "-Werror"
  "-Wfatal-errors"
  "-ffast-math"
  "-fdata-sections"
  "-ffunction-sections"
  "$<$<CONFIG:DEBUG>:-O0;-g3;-ggdb>"
)

set(EXE_FLAGS "-Wl,--gc-sections -z noexecstack")
set(CMAKE_C_STANDARD 11)

if (ENABLE_OPENGL)
  add_definitions(-D ENABLE_OPENGL)
endif()

if (ENABLE_EGL)
  add_definitions(-D ENABLE_EGL)
endif()

if(ENABLE_ASAN)
  add_compile_options("-fno-omit-frame-pointer" "-fsanitize=address")
  set(EXE_FLAGS "${EXE_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
endif()

if(ENABLE_UBSAN)
  add_compile_options("-fsanitize=undefined")
  set(EXE_FLAGS "${EXE_FLAGS} -fsanitize=undefined")
endif()

add_definitions(-D ATOMIC_LOCKING)
add_definitions(-D GL_GLEXT_PROTOTYPES)

add_custom_command(
  OUTPUT  ${CMAKE_BINARY_DIR}/version.c
    ${CMAKE_BINARY_DIR}/_version.c
  COMMAND ${CMAKE_COMMAND} -D PROJECT_TOP=${PROJECT_TOP} -P
    ${PROJECT_TOP}/version.cmake
)

include_directories(
  ${PROJECT_TOP}
  ${PROJECT_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/include
  ${PROJECT_TOP}/repos/nanosvg/src
)

link_libraries(
  ${CMAKE_DL_LIBS}
  rt
  m
)

set(SOURCES
  ${CMAKE_BINARY_DIR}/version.c
  src/main.c
  src/core.c
  src/app.c
  src/message.c
  src/audio.c
  src/config.c
  src/keybind.c
  src/util.c
  src/clipboard.c
  src/kb.c
  src/gl_dynprocs.c
  src/egl_dynprocs.c
  src/eglutil.c
  src/overlay_utils.c
  src/render_queue.c
  src/evdev.c

  src/overlay/splash.c
  src/overlay/alert.c
  src/overlay/fps.c
  src/overlay/graphs.c
  src/overlay/help.c
  src/overlay/config.c
  src/overlay/msg.c
  src/overlay/status.c
)

# Force cimgui to build as a static library.
set(IMGUI_STATIC "yes" CACHE STRING "Build as a static library")
add_definitions("-DCIMGUI_USE_OPENGL2=1")
add_definitions("-DCIMGUI_USE_OPENGL3=1")

add_subdirectory("${PROJECT_TOP}/resources"       "${CMAKE_BINARY_DIR}/resources")
add_subdirectory("${PROJECT_TOP}/common"          "${CMAKE_BINARY_DIR}/common"   )
add_subdirectory("${PROJECT_TOP}/repos/LGMP/lgmp" "${CMAKE_BINARY_DIR}/LGMP"     )
add_subdirectory("${PROJECT_TOP}/repos/PureSpice" "${CMAKE_BINARY_DIR}/PureSpice")
add_subdirectory("${PROJECT_TOP}/repos/cimgui"    "${CMAKE_BINARY_DIR}/cimgui" EXCLUDE_FROM_ALL)

add_subdirectory(displayservers)
add_subdirectory(renderers)

configure_file("${PROJECT_TOP}/resources/looking-glass-client.desktop.in" "${CMAKE_BINARY_DIR}/resources/looking-glass-client.desktop" @ONLY)
add_executable(looking-glass-client ${SOURCES})

target_compile_definitions(looking-glass-client PRIVATE CIMGUI_DEFINE_ENUMS_AND_STRUCTS=1)

target_link_libraries(looking-glass-client
  ${EXE_FLAGS}
  PkgConfig::FONTCONFIG
  lg_resources
  lg_common
  displayservers
  lgmp
  purespice
  renderers
  cimgui
)

if (ENABLE_PIPEWIRE OR ENABLE_PULSEAUDIO)
  add_definitions(-D ENABLE_AUDIO)
  add_subdirectory(audiodevs)
  pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
  target_link_libraries(looking-glass-client
    PkgConfig::SAMPLERATE
    audiodevs
  )
endif()

install(TARGETS looking-glass-client
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT binary)

install(FILES "${CMAKE_BINARY_DIR}/resources/looking-glass-client.desktop"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
install(FILES "${PROJECT_TOP}/resources/lg-logo.svg"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
        RENAME "looking-glass.svg")

feature_summary(WHAT ENABLED_FEATURES DISABLED_FEATURES)
