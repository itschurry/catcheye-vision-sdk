cmake_minimum_required(VERSION 3.20)

project(catcheye-vision-http
    VERSION 0.1.0
    DESCRIPTION "CatchEye Vision HTTP"
    LANGUAGES CXX
)

option(CATCHEYE_VISION_HTTP_BUILD_TESTS "Build tests" ON)

add_library(catcheye_vision_http STATIC
    src/http_server.cpp
    src/roi_api.cpp
)
add_library(catcheye::vision_http ALIAS catcheye_vision_http)
set_target_properties(catcheye_vision_http PROPERTIES EXPORT_NAME vision_http)

target_include_directories(catcheye_vision_http
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(catcheye_vision_http
    PUBLIC
        catcheye::vision_roi
)

target_compile_features(catcheye_vision_http PUBLIC cxx_std_20)

if(CATCHEYE_VISION_HTTP_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(catcheye_vision_http_tests
        tests/test_roi_api.cpp
    )

    target_link_libraries(catcheye_vision_http_tests
        PRIVATE
            catcheye::vision_http
    )

    add_test(
        NAME catcheye_vision_http_tests
        COMMAND catcheye_vision_http_tests
    )
endif()
