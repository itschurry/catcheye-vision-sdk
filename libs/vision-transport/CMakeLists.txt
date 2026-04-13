cmake_minimum_required(VERSION 3.20)

project(catcheye-vision-transport
    VERSION 0.1.0
    DESCRIPTION "CatchEye Vision Transport"
    LANGUAGES CXX
)

add_library(catcheye_vision_transport STATIC
    src/websocket_publisher.cpp
)
add_library(catcheye::vision_transport ALIAS catcheye_vision_transport)
set_target_properties(catcheye_vision_transport PROPERTIES EXPORT_NAME vision_transport)

target_include_directories(catcheye_vision_transport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(catcheye_vision_transport
    PUBLIC
        catcheye::vision_protocol
)

target_compile_features(catcheye_vision_transport PUBLIC cxx_std_20)
