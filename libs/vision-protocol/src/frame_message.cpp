#include "catcheye/protocol/frame_message.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

#include <opencv2/imgcodecs.hpp>

namespace catcheye::protocol {

FrameMessage encode_jpeg_frame(
    const cv::Mat& frame,
    std::string metadata_json,
    std::string stream_name,
    int jpeg_quality)
{
    if (frame.empty()) {
        throw std::runtime_error("cannot encode empty frame");
    }

    FrameMessage message;
    message.stream_name = std::move(stream_name);
    message.metadata_json = std::move(metadata_json);

    const std::vector<int> encode_params {
        cv::IMWRITE_JPEG_QUALITY,
        jpeg_quality,
    };

    if (!cv::imencode(".jpg", frame, message.payload, encode_params)) {
        throw std::runtime_error("failed to encode JPEG frame");
    }

    return message;
}

} // namespace catcheye::protocol
