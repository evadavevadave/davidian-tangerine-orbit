# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the bundle on any platform. On Windows, build.bat does the same without CMake.
cmake_minimum_required(VERSION 3.16)
project(tangerine_orbit C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(WIN32)
    set(LIB_SUFFIX .dll)
    set(UI_CLASS ui:WindowsUI)
    set(WITH_UI ON)
elseif(APPLE)
    set(LIB_SUFFIX .dylib)
    set(WITH_UI OFF) # no Cocoa window layer yet
else()
    set(LIB_SUFFIX .so)
    set(UI_CLASS ui:X11UI)
    find_package(X11)
    set(WITH_UI ${X11_FOUND})
endif()

set(BUNDLE_DIR ${CMAKE_BINARY_DIR}/tangerine_orbit.lv2)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_library(tangerine_orbit MODULE src/plugin.c)
set_target_properties(tangerine_orbit PROPERTIES
    PREFIX ""
    SUFFIX ${LIB_SUFFIX}
    C_STANDARD 11
    C_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>")

if(WITH_UI)
    add_library(tangerine_orbit_ui MODULE src/ui/ui_lv2.c)
    set_target_properties(tangerine_orbit_ui PROPERTIES
        PREFIX ""
        SUFFIX ${LIB_SUFFIX}
        C_STANDARD 11
        C_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>")
    if(WIN32)
        target_link_libraries(tangerine_orbit_ui PRIVATE user32 gdi32)
    else()
        target_include_directories(tangerine_orbit_ui PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(tangerine_orbit_ui PRIVATE ${X11_LIBRARIES} m)
    endif()
endif()

add_executable(test_dsp test/test_dsp.c)
set_target_properties(test_dsp PROPERTIES C_STANDARD 11)
target_link_libraries(test_dsp PRIVATE ${CMAKE_DL_LIBS})

add_executable(ui_render test/ui_render.c)
set_target_properties(ui_render PROPERTIES C_STANDARD 11)

if(NOT MSVC)
    target_link_libraries(tangerine_orbit PRIVATE m)
    target_link_libraries(test_dsp PRIVATE m)
    target_link_libraries(ui_render PRIVATE m)
endif()

# The manifest names the Windows libraries and UI type, so other platforms get theirs.
file(READ bundle/manifest.ttl MANIFEST)
string(REPLACE "tangerine_orbit.dll" "tangerine_orbit${LIB_SUFFIX}" MANIFEST "${MANIFEST}")
string(REPLACE "tangerine_orbit_ui.dll" "tangerine_orbit_ui${LIB_SUFFIX}" MANIFEST "${MANIFEST}")
if(WITH_UI)
    string(REPLACE "ui:WindowsUI" "${UI_CLASS}" MANIFEST "${MANIFEST}")
else()
    string(REGEX REPLACE "# UI-BEGIN.*# UI-END\n?" "" MANIFEST "${MANIFEST}")
endif()
file(WRITE ${BUNDLE_DIR}/manifest.ttl "${MANIFEST}")
configure_file(bundle/tangerine_orbit.ttl ${BUNDLE_DIR}/tangerine_orbit.ttl COPYONLY)

# Licence, notices and the AI usage statement travel with the bundle.
foreach(DOC LICENSE NOTICE.md AI_USAGE.md)
    configure_file(${DOC} ${BUNDLE_DIR}/${DOC} COPYONLY)
endforeach()
message(STATUS "Davidian Tangerine Orbit: GPL-3.0-or-later, made with generative AI (Claude Code); see AI_USAGE.md")

enable_testing()
add_test(NAME test_dsp COMMAND test_dsp ${BUNDLE_DIR})
add_test(NAME ui_render COMMAND ui_render ${CMAKE_BINARY_DIR}/panel.bmp)
