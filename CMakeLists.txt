
cmake_minimum_required(VERSION 3.11)
project(beamer2mlt)

find_package(PkgConfig)
pkg_check_modules(mlt REQUIRED IMPORTED_TARGET mlt++-7)
pkg_check_modules(poppler REQUIRED IMPORTED_TARGET poppler-qt6)

find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

set(CMAKE_CXX_STANDARD 20)

add_executable(beamer2mlt
    main.cpp
)
target_link_libraries(beamer2mlt
    PkgConfig::mlt
    PkgConfig::poppler
    Qt6::Core
    Qt6::Widgets
)
