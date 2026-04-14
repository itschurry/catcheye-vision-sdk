cmake_minimum_required(VERSION 3.20)

project(catcheye-vision-roi
    VERSION 0.1.0
    DESCRIPTION "CatchEye Vision ROI"
    LANGUAGES CXX
)

option(CATCHEYE_VISION_ROI_BUILD_TESTS "Build tests" ON)

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

add_library(catcheye_vision_roi STATIC
    src/roi_evaluator.cpp
    src/roi_geometry.cpp
    src/roi_repository.cpp
    src/roi_validation.cpp
)
add_library(catcheye::vision_roi ALIAS catcheye_vision_roi)
set_target_properties(catcheye_vision_roi PROPERTIES EXPORT_NAME vision_roi)

target_include_directories(catcheye_vision_roi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(catcheye_vision_roi PUBLIC cxx_std_20)

if(CATCHEYE_VISION_ROI_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(catcheye_vision_roi_tests
        tests/test_main.cpp
        tests/test_roi_evaluator.cpp
        tests/test_roi_geometry.cpp
        tests/test_roi_repository.cpp
        tests/test_roi_validation.cpp
    )

    target_link_libraries(catcheye_vision_roi_tests
        PRIVATE
            catcheye::vision_roi
    )

    add_test(
        NAME catcheye_vision_roi_tests
        COMMAND catcheye_vision_roi_tests
    )
endif()
