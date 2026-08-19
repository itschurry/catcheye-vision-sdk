find_path(TensorRT_INCLUDE_DIR
    NAMES NvInfer.h
    HINTS
        /usr/include
        /usr/local/include
        /usr/include/${CMAKE_LIBRARY_ARCHITECTURE}
        /usr/local/cuda/include
)

find_library(TensorRT_NVINFER_LIBRARY
    NAMES nvinfer
    HINTS
        /usr/lib
        /usr/local/lib
        /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
        /usr/local/cuda/lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TensorRT
    REQUIRED_VARS TensorRT_INCLUDE_DIR TensorRT_NVINFER_LIBRARY
)

if(TensorRT_FOUND AND NOT TARGET TensorRT::nvinfer)
    add_library(TensorRT::nvinfer UNKNOWN IMPORTED)
    set_target_properties(TensorRT::nvinfer PROPERTIES
        IMPORTED_LOCATION "${TensorRT_NVINFER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TensorRT_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(TensorRT_INCLUDE_DIR TensorRT_NVINFER_LIBRARY)
