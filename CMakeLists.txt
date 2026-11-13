cmake_minimum_required(VERSION 3.20)

# CMake initializes Apple deployment defaults during the first project() call.
# Explicit settings and toolchains retain ownership of their target platform.
if (CMAKE_HOST_APPLE AND (NOT CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        AND NOT CMAKE_TOOLCHAIN_FILE AND "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL ""
        AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target")
endif ()

project(hoku VERSION 0.1.0 LANGUAGES NONE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (APPLE)
    add_compile_options(-fexperimental-library)
    add_link_options(-fexperimental-library)
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/HuxerUIProject.cmake")
if (HUXERUI_PROJECT_PLAN_ONLY)
    return()
endif ()

file(GLOB_RECURSE APP_SOURCE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)
if (NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    if (WIN32)
        list(APPEND APP_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/main.cpp")
    elseif (APPLE AND NOT IOS)
        list(APPEND APP_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/main.cpp")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND APP_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/main.cpp")
    endif ()
endif ()

huxerui_add_app(hoku
        SOURCES
            ${APP_SOURCE_FILES}
        RESOURCES
            resources
        RESOURCE_NAMESPACE
            app
        ${HUXERUI_APP_RESOURCE_OUTPUT_ARGUMENTS}
        BUNDLE_NAME
            "Hoku"
        BUNDLE_IDENTIFIER
            "dev.farna.hoku"
)

huxerui_configure_project_app(hoku)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HOKU_WEBKITGTK QUIET webkitgtk-6.0>=2.40)
    find_path(HOKU_LINUX_PLATFORM_REGISTRY_DIR
            NAMES huxerui/linux/platform_registry.h
            HINTS "${HUXERUI_HOME}/include" "$ENV{HUXERUI_HOME}/include"
    )
    if (NOT HOKU_WEBKITGTK_FOUND)
        message(FATAL_ERROR
                "Hoku requires WebKitGTK 6.0 >= 2.40 development files "
                "(Fedora: webkitgtk6.0-devel; Debian/Ubuntu: libwebkitgtk-6.0-dev)"
        )
    endif ()
    if (NOT HOKU_LINUX_PLATFORM_REGISTRY_DIR)
        message(FATAL_ERROR
                "Hoku on Linux requires the FarnaHerry/HuxerUI fork with the GTK4 PlatformView host; "
                "set HUXERUI_HOME to that source checkout"
        )
    endif ()
endif ()

set(HOKU_WEBVIEW_PATH "$ENV{HOKU_WEBVIEW_PATH}" CACHE PATH
        "Optional local FarnaHerry/Lib-WebView checkout")
if (HOKU_WEBVIEW_PATH)
    get_filename_component(HOKU_WEBVIEW_PATH "${HOKU_WEBVIEW_PATH}" ABSOLUTE)
    huxerui_use_library(hoku
            TARGET HuxerUI::WebView
            PATH "${HOKU_WEBVIEW_PATH}"
    )
else ()
    huxerui_use_library(hoku
            TARGET HuxerUI::WebView
            URL "https://github.com/FarnaHerry/Lib-WebView.git"
            COMMIT "09c820a1d8d10237148b3e1d103beaacdcee0034"
    )
endif ()

if (APPLE AND NOT IOS AND NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    target_link_libraries(hoku PRIVATE "-framework Foundation" "-framework AppKit")
endif ()

include(CTest)
if (BUILD_TESTING AND NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    add_executable(hoku_core_tests
            tests/browser_core_test.cpp
            src/browser_session.cpp
            src/destination.cpp
    )
    target_compile_features(hoku_core_tests PRIVATE cxx_std_20)
    target_include_directories(hoku_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME hoku_core COMMAND hoku_core_tests)
endif ()
