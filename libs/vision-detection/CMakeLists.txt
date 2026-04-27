cmake_minimum_required(VERSION 3.20)

project(catcheye-vision-detection
    VERSION 0.1.0
    DESCRIPTION "CatchEye Vision Detection"
    LANGUAGES CXX
)

option(CATCHEYE_VISION_DETECTION_ENABLE_NCNN "Build NCNN detector backend" ON)

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

if(NOT TARGET catcheye_vision_input AND NOT TARGET catcheye::vision_input)
    set(CATCHEYE_VISION_INPUT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vision-input")
    if(NOT EXISTS "${CATCHEYE_VISION_INPUT_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "catcheye-vision-input repo not found at ${CATCHEYE_VISION_INPUT_SOURCE_DIR}")
    endif()

    add_subdirectory(
        "${CATCHEYE_VISION_INPUT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/catcheye-vision-input"
    )
endif()

set(CATCHEYE_VISION_DETECTION_SOURCES)

if(CATCHEYE_VISION_DETECTION_ENABLE_NCNN)
    find_package(OpenCV REQUIRED)
    find_package(ncnn REQUIRED)
    find_package(yaml-cpp REQUIRED)

    list(APPEND CATCHEYE_VISION_DETECTION_SOURCES
        src/ncnn/ncnn_detector.cpp
    )
endif()

add_library(catcheye_vision_detection STATIC
    ${CATCHEYE_VISION_DETECTION_SOURCES}
)
add_library(catcheye::vision_detection ALIAS catcheye_vision_detection)
set_target_properties(catcheye_vision_detection PROPERTIES EXPORT_NAME vision_detection)

target_include_directories(catcheye_vision_detection
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(catcheye_vision_detection
    PUBLIC
        catcheye::vision_input
)

target_compile_features(catcheye_vision_detection PUBLIC cxx_std_20)

if(CATCHEYE_VISION_DETECTION_ENABLE_NCNN)
    target_include_directories(catcheye_vision_detection
        PRIVATE
            ${OpenCV_INCLUDE_DIRS}
    )

    target_link_libraries(catcheye_vision_detection
        PUBLIC
            ncnn
        PRIVATE
            ${OpenCV_LIBS}
            yaml-cpp::yaml-cpp
    )

    target_compile_definitions(catcheye_vision_detection
        PUBLIC
            CATCHEYE_VISION_DETECTION_HAS_NCNN=1
    )
endif()
