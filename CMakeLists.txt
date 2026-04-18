cmake_minimum_required(VERSION 3.16)
project(5250script VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

# Library sources
set(LIB_HEADERS
    include/5250script/screen_interface.h
    include/5250script/script_token.h
    include/5250script/script_ast.h
    include/5250script/script_lexer.h
    include/5250script/script_parser.h
    include/5250script/script_executor.h
    include/5250script/script_compiler.h
)

set(LIB_SOURCES
    src/script_lexer.cpp
    src/script_parser.cpp
    src/script_executor.cpp
    src/script_compiler.cpp
)

add_library(5250script STATIC ${LIB_SOURCES} ${LIB_HEADERS})

target_include_directories(5250script
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/5250script
)

target_link_libraries(5250script PUBLIC Qt6::Core)

# Testing
option(BUILD_5250SCRIPT_TESTS "Build 5250script tests" OFF)
if(BUILD_5250SCRIPT_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    macro(add_5250script_test TEST_NAME TEST_SOURCE)
        add_executable(${TEST_NAME} ${TEST_SOURCE})
        target_link_libraries(${TEST_NAME} PRIVATE 5250script Qt6::Test)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endmacro()

    add_5250script_test(test_script_lexer tests/test_script_lexer.cpp)
    add_5250script_test(test_script_parser tests/test_script_parser.cpp)
    add_5250script_test(test_script_executor_bounds tests/test_script_executor_bounds.cpp)
endif()
