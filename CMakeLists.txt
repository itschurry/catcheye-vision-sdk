cmake_minimum_required(VERSION 3.20)

project(catcheye-vision-sdk
    VERSION 0.1.0
    DESCRIPTION "CatchEye Vision SDK"
    LANGUAGES CXX
)

option(CATCHEYE_VISION_SDK_BUILD_TESTS "Build tests for SDK libraries" ON)

set(CATCHEYE_VISION_INPUT_BUILD_TESTS ${CATCHEYE_VISION_SDK_BUILD_TESTS} CACHE BOOL "" FORCE)
set(CATCHEYE_VISION_ROI_BUILD_TESTS ${CATCHEYE_VISION_SDK_BUILD_TESTS} CACHE BOOL "" FORCE)
set(CATCHEYE_VISION_RUNTIME_BUILD_TESTS ${CATCHEYE_VISION_SDK_BUILD_TESTS} CACHE BOOL "" FORCE)

add_subdirectory(libs/vision-input)
add_subdirectory(libs/vision-detection)
add_subdirectory(libs/vision-roi)
add_subdirectory(libs/vision-protocol)
add_subdirectory(libs/vision-transport)
add_subdirectory(libs/vision-runtime)
