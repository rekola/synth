cmake_minimum_required(VERSION 3.10)

# Cell UI: what the terminal (and later a window) UI is built from. It builds
# on its own and includes nothing from the application: everything it needs
# is under this directory.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(cellui LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED True)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
  endif()
  add_compile_options(-Wall -Wextra)
  enable_testing()
  set(CELLUI_STANDALONE ON)
endif()

# libunistring backs cellui/Utf8.h's grapheme-cluster-aware width and
# truncation. No pkg-config file ships with libunistring-dev on Ubuntu.
find_library(UNISTRING_LIBRARY unistring)
find_path(UNISTRING_INCLUDE_DIR unigbrk.h)
if(NOT (UNISTRING_LIBRARY AND UNISTRING_INCLUDE_DIR))
  message(FATAL_ERROR "libunistring not found (install libunistring-dev)")
endif()

# canvas draws the scopes' pixel versions. Its warnings are its own, so it is
# built from a plain checkout outside this project's flag set (an including
# project may give the flags in CELLUI_CANVAS_CXX_FLAGS).
if(NOT TARGET canvas)
  include(FetchContent)
  FetchContent_Declare(canvas
    GIT_REPOSITORY https://github.com/rekola/canvas
    GIT_TAG 0d437f50c438fecc5e21ef7ebc195bc72ac0935e)
  FetchContent_GetProperties(canvas)
  if(NOT canvas_POPULATED)
    FetchContent_Populate(canvas)
  endif()
  if(NOT DEFINED CELLUI_CANVAS_CXX_FLAGS)
    set(CELLUI_CANVAS_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
  endif()
  set(CANVAS_CXX_FLAGS_SAVED "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CELLUI_CANVAS_CXX_FLAGS}")
  set(CANVAS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  if(WIN32)
    set(CANVAS_BACKEND_CAIRO OFF CACHE BOOL "" FORCE)
    set(CANVAS_BACKEND_GDIPLUS ON CACHE BOOL "" FORCE)
  endif()
  if(APPLE)
    set(CANVAS_BACKEND_CAIRO OFF CACHE BOOL "" FORCE)
    set(CANVAS_BACKEND_QUARTZ ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(${canvas_SOURCE_DIR} ${canvas_BINARY_DIR} EXCLUDE_FROM_ALL)
  set(CMAKE_CXX_FLAGS "${CANVAS_CXX_FLAGS_SAVED}")
endif()

add_library(cellui STATIC
  src/BufferPlane.cpp
  src/CellBuffer.cpp
  src/ChannelMeter.cpp
  src/ChoiceDialog.cpp
  src/Gallery.cpp
  src/InfoDialog.cpp
  src/InlineEditor.cpp
  src/EscapeCoalescer.cpp
  src/EventLoop.cpp
  src/KeyReader.cpp
  src/LineEditor.cpp
  src/Markdown.cpp
  src/MenuBar.cpp
  src/Scopes.cpp
  src/SpectrumMeter.cpp
  src/Scrim.cpp
  src/SubcellGlyphs.cpp
  src/Utf8.cpp
  src/term/BackdropEmulation.cpp
  src/term/Discovery.cpp
  src/term/FrameEncoder.cpp
  src/term/ImageEncoder.cpp
  src/term/InputParser.cpp
  src/term/TermEvent.cpp)
if(NOT WIN32)
  target_sources(cellui PRIVATE src/PollEventLoop.cpp src/term/TerminalSession.cpp src/TestBackend.cpp)
  find_package(Threads REQUIRED)
  target_link_libraries(cellui PUBLIC Threads::Threads)
endif()
# The GTK 4 window backend (cellui/gtk/): CELLUI_ENABLE_GTK, set by the
# including project (SYNTH_ENABLE_GTK) or, standalone, found here.
if(CELLUI_STANDALONE AND NOT DEFINED CELLUI_ENABLE_GTK AND NOT WIN32 AND NOT APPLE)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(CELLUI_GTK4_PROBE QUIET gtk4>=4.10)
  endif()
  if(CELLUI_GTK4_PROBE_FOUND)
    set(CELLUI_ENABLE_GTK ON)
  endif()
endif()
if(CELLUI_ENABLE_GTK)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(CELLUI_GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.10)
  target_sources(cellui PRIVATE src/gtk/GlibEventLoop.cpp src/gtk/Window.cpp)
  target_link_libraries(cellui PRIVATE PkgConfig::CELLUI_GTK4)
  target_compile_definitions(cellui PUBLIC CELLUI_HAS_GTK)
  # Needs a display: run under Xvfb (xvfb-run) when there is none, skipped
  # when neither exists.
  add_executable(cellui_gtk_tests tests/test_main.cpp tests/GtkWindowTests.cpp)
  target_link_libraries(cellui_gtk_tests PRIVATE cellui)
  find_program(CELLUI_XVFB_RUN xvfb-run)
  if(CELLUI_XVFB_RUN)
    add_test(NAME cellui_gtk_tests COMMAND ${CELLUI_XVFB_RUN} -a $<TARGET_FILE:cellui_gtk_tests>)
  else()
    add_test(NAME cellui_gtk_tests COMMAND cellui_gtk_tests)
  endif()
  set_tests_properties(cellui_gtk_tests PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: no display")
endif()
# The pixel versions draw with canvas' Cairo backend, or its Quartz 2D one on
# Apple platforms; the GDI+ one joins with the window backend on Windows.
if(NOT WIN32)
  target_sources(cellui PRIVATE src/CanvasRasterizer.cpp src/HeatmapImage.cpp src/NativeContext.cpp src/PixelScopes.cpp src/SpectrumImage.cpp src/VuMeterImage.cpp)
  if(NOT APPLE)
    # Text in a font the one asked for lacks is found with fontconfig (Core
    # Text falls back by itself).
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CELLUI_FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
    target_link_libraries(cellui PRIVATE PkgConfig::CELLUI_FONTCONFIG)
  endif()
  target_compile_definitions(cellui PRIVATE CELLUI_PIXEL_SCOPES)
endif()
# canvas' headers are reached as SYSTEM includes.
target_include_directories(cellui SYSTEM PUBLIC ${canvas_SOURCE_DIR}/include)
target_link_libraries(cellui PUBLIC canvas)
target_include_directories(cellui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(cellui PRIVATE ${UNISTRING_INCLUDE_DIR})
target_link_libraries(cellui PUBLIC ${UNISTRING_LIBRARY})

# Prints the gallery page (every style and widget the library has).
add_executable(cellui_gallery gallery/main.cpp)
target_link_libraries(cellui_gallery PRIVATE cellui)

# The library's own tests: plain functions registered with TEST(), run by
# tests/test_main.cpp; no framework dependency.
add_executable(cellui_tests
  tests/test_main.cpp
  tests/BufferPlaneTests.cpp
  tests/CellBufferTests.cpp
  tests/ChoiceDialogTests.cpp
  tests/ChoiceListTests.cpp
  tests/ColorTests.cpp
  tests/DiscoveryTests.cpp
  tests/ElementTests.cpp
  tests/FrameEncoderTests.cpp
  tests/ImageEncoderTests.cpp
  tests/GalleryTests.cpp
  tests/HeatmapChartTests.cpp
  tests/InfoDialogTests.cpp
  tests/MarkdownTests.cpp
  tests/ThemeTests.cpp
  tests/SpinBoxTests.cpp
  tests/SubcellGlyphsTests.cpp
  tests/InlineEditorTests.cpp
  tests/InputParserTests.cpp
  tests/KeymapTests.cpp
  tests/LevelMeterTests.cpp
  tests/EscapeCoalescerTests.cpp
  tests/KeyReaderTests.cpp
  tests/LineEditorTests.cpp
  tests/MenuBarTests.cpp
  tests/MeterTests.cpp
  tests/Utf8Tests.cpp)
if(NOT WIN32)
  target_sources(cellui_tests PRIVATE tests/BackdropEmulationTests.cpp tests/CanvasRasterizerTests.cpp tests/EventLoopTests.cpp tests/TerminalSessionTests.cpp tests/TestBackendTests.cpp tests/HeatmapImageTests.cpp tests/SpectrumImageTests.cpp
    tests/VuMeterTests.cpp)
endif()
target_link_libraries(cellui_tests PRIVATE cellui)
if(NOT WIN32 AND NOT APPLE)
  target_link_libraries(cellui_tests PRIVATE PkgConfig::CELLUI_FONTCONFIG)
endif()
target_compile_definitions(cellui_tests PRIVATE CELLUI_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_test(NAME cellui_tests COMMAND cellui_tests)

# Nothing in the library may reach into the application.
add_test(NAME cellui_includes_nothing_from_src
  COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_includes.cmake)
