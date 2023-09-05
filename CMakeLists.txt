cmake_minimum_required(VERSION 3.9)

project(dirage2 VERSION 1.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
        main.cpp
        controller.h controller.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        agechart.h agechart.cpp
        dirtree.h dirtree.cpp
        dirmodel.h dirmodel.cpp
        scannerservice.h scannerservice.cpp
        searchservice.h searchservice.cpp
        chartcalculatorservice.h chartcalculatorservice.cpp
        agechartitemdelegate.h agechartitemdelegate.cpp
        savereportservice.h savereportservice.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(dirage2
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET dirage2 APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(dirage2 SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(dirage2
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(dirage2 PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

#set_property(TARGET dirage2 PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

install(TARGETS dirage2
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(dirage2)
endif()
