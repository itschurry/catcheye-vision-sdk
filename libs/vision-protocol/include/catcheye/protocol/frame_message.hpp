#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

namespace catcheye::protocol {

struct FrameMessage {
    std::string stream_name;
    std::string mime_type = "image/jpeg";
    std::string metadata_json = "{}";
    std::vector<std::uint8_t> payload;

    bool empty() const
    {
        return payload.empty();
    }
};

FrameMessage encode_jpeg_frame(
    const cv::Mat& frame,
    std::string metadata_json,
    std::string stream_name = "preview",
    int jpeg_quality = 80);

} // namespace catcheye::protocol
