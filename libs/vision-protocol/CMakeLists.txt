cmake_minimum_required(VERSION 3.20)

project(catcheye-vision-protocol
    VERSION 0.1.0
    DESCRIPTION "CatchEye Vision Protocol"
    LANGUAGES CXX
)

find_package(OpenCV REQUIRED)

add_library(catcheye_vision_protocol STATIC
    src/frame_message.cpp
)
add_library(catcheye::vision_protocol ALIAS catcheye_vision_protocol)
set_target_properties(catcheye_vision_protocol PROPERTIES EXPORT_NAME vision_protocol)

target_include_directories(catcheye_vision_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(catcheye_vision_protocol
    PUBLIC
        ${OpenCV_LIBS}
)

target_compile_features(catcheye_vision_protocol PUBLIC cxx_std_20)
