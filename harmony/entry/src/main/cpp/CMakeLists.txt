cmake_minimum_required(VERSION 3.5.0)
project(geany_harmony_qt)
set_property(GLOBAL PROPERTY JOB_POOLS compile_pool=2 link_pool=1)
set(CMAKE_JOB_POOL_COMPILE compile_pool)
set(CMAKE_JOB_POOL_LINK link_pool)
set(CMAKE_AUTOGEN_PARALLEL 2)

set(CMAKE_AUTOMOC ON)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(HARMONY_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
get_filename_component(GEANY_ROOT "${HARMONY_PROJECT_ROOT}/.." ABSOLUTE)

if (NOT IS_ABSOLUTE "${QT_PREFIX}")
    get_filename_component(QT_PREFIX "${HARMONY_PROJECT_ROOT}/${QT_PREFIX}" ABSOLUTE)
endif()

if (NOT EXISTS "${QT_PREFIX}/lib/cmake/Qt5/Qt5Config.cmake")
    message(FATAL_ERROR "QT_PREFIX is invalid: ${QT_PREFIX}")
endif()

list(PREPEND CMAKE_PREFIX_PATH ${QT_PREFIX})
list(APPEND CMAKE_FIND_ROOT_PATH ${QT_PREFIX})

find_package(QT NAMES Qt5 REQUIRED COMPONENTS Core Widgets PrintSupport)
find_package(Qt5 REQUIRED COMPONENTS
    Core
    Gui
    OhExtras
    PrintSupport
    Widgets
)

add_library(entry SHARED
    geany_qt_harmony.cpp
)

target_compile_features(entry PRIVATE cxx_std_17)
target_compile_definitions(entry PRIVATE
    GEANY_HARMONY_QT
    OPENHARMONY
    NAPI_DISABLE_CPP_EXCEPTIONS
)

target_include_directories(entry PRIVATE
    ${GEANY_ROOT}/scintilla/include
)

set_target_properties(entry PROPERTIES
    BUILD_RPATH "$ORIGIN"
)

target_link_libraries(entry PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::OhExtras
    Qt5::PrintSupport
    Qt5::Widgets
    Qt5::QHarmonyStylePlugin
    Qt5::QOpenHarmonyPlatformIntegrationPlugin
    Qt5::QGifPlugin
    Qt5::QICOPlugin
    Qt5::QJpegPlugin
)

if(QT_HOST_MOC)
    set_target_properties(entry PROPERTIES AUTOMOC_EXECUTABLE "${QT_HOST_MOC}")
endif()
