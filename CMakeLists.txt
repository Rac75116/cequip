cmake_minimum_required(VERSION 3.20)
project(cequip VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    message(STATUS "Building for Windows")
elseif(APPLE)
    message(STATUS "Building for macOS")
elseif(UNIX)
    message(STATUS "Building for Linux")
endif()

add_executable(cequip
    src/main.cpp
)

find_package(Boost REQUIRED COMPONENTS
    wave
)
target_include_directories(cequip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
)
target_link_libraries(cequip PRIVATE
    Boost::wave
)

find_package(CLI11 CONFIG REQUIRED)
target_link_libraries(cequip PRIVATE CLI11::CLI11)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(cequip PRIVATE spdlog::spdlog)

target_precompile_headers(cequip PRIVATE
    <spdlog/spdlog.h>
    <CLI/CLI.hpp>
    <boost/wave.hpp>
    <boost/wave/cpplexer/cpp_lex_iterator.hpp>
)

target_compile_definitions(cequip PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")

if(WIN32)
    target_compile_definitions(cequip PRIVATE _WIN32_WINNT=0x0601)
    target_compile_definitions(cequip PRIVATE BOOST_ALL_NO_LIB)
elseif(APPLE)
    target_link_libraries(cequip PRIVATE pthread)
elseif(UNIX)
    target_link_libraries(cequip PRIVATE pthread dl)
endif()

if(MSVC)
    target_compile_options(cequip PRIVATE /W4)
else()
    target_compile_options(cequip PRIVATE -O3 -Wall -Wextra -pedantic)
endif()

install(TARGETS cequip DESTINATION bin)
