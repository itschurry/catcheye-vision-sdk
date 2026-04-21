#pragma once

#include <cstddef>
#include <cstdint>

namespace catcheye::input {

enum class PixelFormat : std::uint8_t { UNKNOWN = 0, RGB, BGR, RGBA, BGRA, GRAY8, NV12 };

constexpr int channels(PixelFormat fmt)
{
    switch (fmt) {
        case PixelFormat::RGB:   return 3;
        case PixelFormat::BGR:   return 3;
        case PixelFormat::RGBA:  return 4;
        case PixelFormat::BGRA:  return 4;
        case PixelFormat::GRAY8: return 1;
        default:                 return 0;  // NV12 and others are planar
    }
}

// Returns total buffer size in bytes given stride (bytes per row) and height.
// NV12: Y plane (stride × height) + UV interleaved plane (stride × height/2)
// Packed formats: stride × height
constexpr std::size_t frame_data_size(PixelFormat fmt, int stride, int height)
{
    const auto s = static_cast<std::size_t>(stride);
    const auto h = static_cast<std::size_t>(height);
    if (fmt == PixelFormat::NV12) {
        return s * h * 3 / 2;
    }
    return s * h;
}

} // namespace catcheye::input
