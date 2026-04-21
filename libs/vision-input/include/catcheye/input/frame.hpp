#pragma once

#include <cstdint>
#include <vector>

#include "catcheye/input/pixel_format.hpp"
#include "catcheye/input/timestamp.hpp"

namespace catcheye::input {

struct Frame {
    std::vector<std::uint8_t> data;
    int width = 0;
    int height = 0;
    int stride = 0;  // bytes per row, may include hardware alignment padding
    PixelFormat format = PixelFormat::UNKNOWN;
    Timestamp timestamp = 0;

    bool empty() const { return data.empty(); }
};

} // namespace catcheye::input
