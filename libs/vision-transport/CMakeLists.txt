cmake_minimum_required(VERSION 3.20)

project(catcheye-vision-transport
    VERSION 0.1.0
    DESCRIPTION "CatchEye Vision Transport"
    LANGUAGES CXX
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
pkg_check_modules(GST_RTSP REQUIRED IMPORTED_TARGET gstreamer-rtsp-server-1.0)
find_package(OpenCV REQUIRED)

add_library(catcheye_vision_transport STATIC
    src/rtsp_publisher.cpp
    src/websocket_publisher.cpp
)
add_library(catcheye::vision_transport ALIAS catcheye_vision_transport)
set_target_properties(catcheye_vision_transport PROPERTIES EXPORT_NAME vision_transport)

target_include_directories(catcheye_vision_transport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${GSTREAMER_INCLUDE_DIRS}
        ${GST_RTSP_INCLUDE_DIRS}
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(catcheye_vision_transport
    PUBLIC
        catcheye::vision_protocol
        catcheye::vision_input
        PkgConfig::GSTREAMER
        PkgConfig::GST_RTSP
        ${OpenCV_LIBS}
)

target_compile_features(catcheye_vision_transport PUBLIC cxx_std_20)
