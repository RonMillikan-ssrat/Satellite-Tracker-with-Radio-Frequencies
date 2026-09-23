cmake_minimum_required(VERSION 3.16)
project(SatelliteTracker VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Enable debug symbols and disable optimization for Debug builds
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# Export compile commands for IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# Vendored SGP4 library (https://github.com/dnwrnr/sgp4)
file(GLOB SGP4_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/external/sgp4/libsgp4/*.cc)
add_library(sgp4 STATIC ${SGP4_SOURCES})
set_target_properties(sgp4 PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(sgp4 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/sgp4)
if(NOT MSVC)
    target_compile_options(sgp4 PRIVATE -w)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/satellitetracker.cpp
    src/skymapwidget.cpp
    src/sgp4wrapper.cpp
    src/tleparser.cpp
    src/apiserver.cpp
)

set(HEADERS
    include/mainwindow.h
    include/satellitetracker.h
    include/skymapwidget.h
    include/sgp4wrapper.h
    include/tleparser.h
    include/satellite.h
    include/apiserver.h
)

# Add executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link Qt libraries
target_link_libraries(${PROJECT_NAME} 
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    sgp4
)

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
