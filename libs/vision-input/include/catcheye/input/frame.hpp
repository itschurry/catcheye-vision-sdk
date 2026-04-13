#pragma once

#include <opencv2/core/mat.hpp>

#include "catcheye/input/pixel_format.hpp"
#include "catcheye/input/timestamp.hpp"

namespace catcheye::input {

struct Frame {
    cv::Mat image;
    PixelFormat format = PixelFormat::UNKNOWN;
    Timestamp timestamp = 0;

    bool empty() const {
        return image.empty();
    }

    int width() const {
        return image.cols;
    }

    int height() const {
        return image.rows;
    }
};

} // namespace catcheye::input
