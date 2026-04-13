cmake_minimum_required(VERSION 3.20)

project(catcheye-vision-input
    VERSION 0.1.0
    DESCRIPTION "CatchEye Vision Input"
    LANGUAGES CXX
)

option(CATCHEYE_VISION_INPUT_BUILD_TESTS "Build tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
elseif(MSVC)
    add_compile_options(/W4)
endif()

find_package(OpenCV REQUIRED)

add_library(catcheye_vision_input STATIC
    src/frame_source.cpp
)
add_library(catcheye::vision_input ALIAS catcheye_vision_input)
set_target_properties(catcheye_vision_input PROPERTIES EXPORT_NAME vision_input)

target_include_directories(catcheye_vision_input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(catcheye_vision_input
    PUBLIC
        ${OpenCV_LIBS}
)

target_compile_features(catcheye_vision_input PUBLIC cxx_std_20)

if(CATCHEYE_VISION_INPUT_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(catcheye_vision_input_tests
        tests/test_main.cpp
        tests/test_frame_source.cpp
    )

    target_link_libraries(catcheye_vision_input_tests
        PRIVATE
            catcheye::vision_input
    )

    add_test(
        NAME catcheye_vision_input_tests
        COMMAND catcheye_vision_input_tests
    )
endif()
