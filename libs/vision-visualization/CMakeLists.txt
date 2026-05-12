cmake_minimum_required(VERSION 3.20)

project(catcheye-vision-visualization
    VERSION 0.1.0
    DESCRIPTION "CatchEye Vision Visualization"
    LANGUAGES CXX
)

option(CATCHEYE_VISION_VISUALIZATION_BUILD_TESTS "Build tests" ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc)

add_library(catcheye_vision_visualization STATIC
    src/annotation_renderer.cpp
)
add_library(catcheye::vision_visualization ALIAS catcheye_vision_visualization)
set_target_properties(catcheye_vision_visualization PROPERTIES EXPORT_NAME vision_visualization)

target_include_directories(catcheye_vision_visualization
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(catcheye_vision_visualization
    PUBLIC
        catcheye::vision_input
        catcheye::vision_detection
        ${OpenCV_LIBS}
)

target_compile_features(catcheye_vision_visualization PUBLIC cxx_std_20)

if(CATCHEYE_VISION_VISUALIZATION_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(catcheye_vision_visualization_tests
        tests/test_annotation_renderer.cpp
    )

    target_link_libraries(catcheye_vision_visualization_tests
        PRIVATE
            catcheye::vision_visualization
    )

    add_test(
        NAME catcheye_vision_visualization_tests
        COMMAND catcheye_vision_visualization_tests
    )
endif()
