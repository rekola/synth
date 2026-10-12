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
  src/Markdown.cpp
  src/SubcellGlyphs.cpp
  src/Utf8.cpp)
target_include_directories(cellui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(cellui PRIVATE ${UNISTRING_INCLUDE_DIR})
target_link_libraries(cellui PUBLIC ${UNISTRING_LIBRARY})

# The library's own tests: plain functions registered with TEST(), run by
# tests/test_main.cpp; no framework dependency.
add_executable(cellui_tests
  tests/test_main.cpp
  tests/ChoiceListTests.cpp
  tests/ColorTests.cpp
  tests/ElementTests.cpp
  tests/MarkdownTests.cpp
  tests/ThemeTests.cpp
  tests/SubcellGlyphsTests.cpp
  tests/KeymapTests.cpp
  tests/Utf8Tests.cpp)
target_link_libraries(cellui_tests PRIVATE cellui)
add_test(NAME cellui_tests COMMAND cellui_tests)

# Nothing in the library may reach into the application.
add_test(NAME cellui_includes_nothing_from_src
  COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_includes.cmake)
