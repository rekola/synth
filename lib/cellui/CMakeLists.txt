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

add_library(cellui STATIC
  src/BufferPlane.cpp
  src/CellBuffer.cpp
  src/ChannelMeter.cpp
  src/ChoiceDialog.cpp
  src/Gallery.cpp
  src/InfoDialog.cpp
  src/InlineEditor.cpp
  src/EscapeCoalescer.cpp
  src/KeyReader.cpp
  src/LineEditor.cpp
  src/Markdown.cpp
  src/MenuBar.cpp
  src/SpectrumMeter.cpp
  src/SubcellGlyphs.cpp
  src/Utf8.cpp
  src/term/Discovery.cpp
  src/term/FrameEncoder.cpp
  src/term/ImageEncoder.cpp
  src/term/InputParser.cpp
  src/term/TermEvent.cpp)
if(NOT WIN32)
  target_sources(cellui PRIVATE src/term/TerminalSession.cpp)
  find_package(Threads REQUIRED)
  target_link_libraries(cellui PUBLIC Threads::Threads)
endif()
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
  target_sources(cellui_tests PRIVATE tests/TerminalSessionTests.cpp)
endif()
target_link_libraries(cellui_tests PRIVATE cellui)
target_compile_definitions(cellui_tests PRIVATE CELLUI_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_test(NAME cellui_tests COMMAND cellui_tests)

# Nothing in the library may reach into the application.
add_test(NAME cellui_includes_nothing_from_src
  COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_includes.cmake)
