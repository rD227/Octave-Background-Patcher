cmake_minimum_required(VERSION 3.16)
project(octave-bg-patcher VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Paths ───────────────────────────────────────────────────────────

# Set these before calling cmake or modify to match your system
set(OCTAVE_ROOT "$ENV{OCTAVE_ROOT}" CACHE PATH "Octave installation root")
if(NOT OCTAVE_ROOT)
    # Try to auto-detect relative to build directory
    get_filename_component(OCTAVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

set(MINGW64_DIR "${OCTAVE_ROOT}/mingw64")
set(QT6_DIR     "${MINGW64_DIR}/qt6")

# ─── Qt6 include & library dirs ──────────────────────────────────────

set(QT6_INCLUDE_DIRS
    "${QT6_DIR}/include"
    "${QT6_DIR}/include/QtCore"
    "${QT6_DIR}/include/QtGui"
    "${QT6_DIR}/include/QtWidgets"
    "${QT6_DIR}/include/Qsci"
)

set(QT6_LIBRARY_DIR "${QT6_DIR}/lib")

# ─── bgpatch.dll (injected DLL) ─────────────────────────────────────

add_library(bgpatch SHARED
    src/bgpatch/main.cpp
    src/bgpatch/bgpatch.cpp
)

target_include_directories(bgpatch PRIVATE ${QT6_INCLUDE_DIRS})
target_link_directories(bgpatch PRIVATE ${QT6_LIBRARY_DIR})

target_link_libraries(bgpatch
    Qt6Core
    Qt6Gui
    Qt6Widgets
    qscintilla2_qt6
)

target_compile_definitions(bgpatch PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

# Ensure DLL exports are minimal
set_target_properties(bgpatch PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ─── patcher.exe (configuration & launcher) ─────────────────────────

add_executable(patcher WIN32
    src/patcher/main.cpp
)

target_compile_definitions(patcher PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_link_libraries(patcher
    comctl32
)

# ─── Install ────────────────────────────────────────────────────────

set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/out" CACHE PATH "Output directory")
install(TARGETS bgpatch patcher
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)
