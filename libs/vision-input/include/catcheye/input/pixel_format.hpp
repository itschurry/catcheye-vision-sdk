#pragma once

#include <cstdint>

namespace catcheye::input {

enum class PixelFormat : std::uint8_t { UNKNOWN = 0, RGB, BGR, RGBA, BGRA, GRAY8, NV12 };

constexpr int channels(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::RGB:
            return 3;
        case PixelFormat::BGR:
            return 3;
        case PixelFormat::RGBA:
            return 4;
        case PixelFormat::BGRA:
            return 4;
        case PixelFormat::GRAY8:
            return 1;
        default:
            return 0;
    }
}

} // namespace catcheye::input
