#pragma once

#include <cstdint>

#include <opencv2/core/mat.hpp>

#include "catcheye/input/frame.hpp"
#include "catcheye/protocol/frame_message.hpp"

namespace catcheye::runtime {

struct ProcessContext {
    std::uint64_t frame_index = 0;
    bool should_process = true;
    bool needs_preview = false;
    bool needs_publish = false;
};

struct ProcessOutput {
    bool has_preview = false;
    cv::Mat preview_frame;
    bool has_message = false;
    catcheye::protocol::FrameMessage message;
};

class FrameProcessor {
   public:
    virtual ~FrameProcessor() = default;

    virtual bool initialize() = 0;
    virtual ProcessOutput process(const catcheye::input::Frame& frame, const ProcessContext& context) = 0;
};

} // namespace catcheye::runtime
