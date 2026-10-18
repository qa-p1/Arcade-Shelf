cmake_minimum_required(VERSION 3.25)
project(ArcadeLinkQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network)

add_library(arcade_link_qt STATIC ArcadeLink.h ArcadeLink.cpp
    ArcadeAccelerator.h ArcadeAccelerator.cpp ArcadeShortcuts.h ArcadeShortcuts.cpp
    ArcadeTrayHost.h ArcadeTrayHost.cpp ArcadeInstall.h ArcadeInstall.cpp ArcadeInstallPlatform.cpp)
target_include_directories(arcade_link_qt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arcade_link_qt PUBLIC Qt6::Core Qt6::Network)
if(MSVC)
    target_compile_options(arcade_link_qt PUBLIC /utf-8)
endif()
if(WIN32)
    target_link_libraries(arcade_link_qt PUBLIC advapi32 user32)
endif()

option(ARCADE_LINK_BUILD_TESTS "Build conformance and interop tests" ON)
if(ARCADE_LINK_BUILD_TESTS)
find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
enable_testing()
add_executable(startup-probe examples/startup-probe.cpp)
target_link_libraries(startup-probe PRIVATE arcade_link_qt)
foreach(test_name IN ITEMS Vectors Server Install TrayHost Methods)
    add_executable(tst_${test_name} tests/tst_${test_name}.cpp)
    target_link_libraries(tst_${test_name} PRIVATE arcade_link_qt Qt6::Test)
    target_compile_definitions(tst_${test_name} PRIVATE
        ARCADE_LINK_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/../spec/vectors"
        ARCADE_LINK_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
        ARCADE_LINK_INSTALL_STRINGS="${CMAKE_CURRENT_SOURCE_DIR}/../assets/strings/install.json")
    # Windows QtTest may otherwise select debugger output instead of stdout.
    add_test(NAME ${test_name} COMMAND tst_${test_name} -o -,txt
        -o "${CMAKE_CURRENT_BINARY_DIR}/${test_name}.txt,txt" -v2)
endforeach()
endif()
